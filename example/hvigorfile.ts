import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, join, resolve } from 'node:path';
import { hapTasks } from '@ohos/hvigor-ohos-plugin';

const REMOVED_SYSCAPS = new Set([
  'SystemCapability.Multimedia.Media.AVTranscoder',
  'SystemCapability.Communication.Bluetooth.Core',
  'SystemCapability.ArkUi.Graphics3D',
  'SystemCapability.DistributedHardware.DeviceManager',
  'SystemCapability.Multimedia.Drm.Core',
  'SystemCapability.Customization.EnterpriseDeviceManager'
]);

function resolveSyscapTool(modulePath: string): string {
  const projectRoot = resolve(modulePath, '..');
  const localPropertiesPath = resolve(projectRoot, 'local.properties');
  const buildProfilePath = resolve(projectRoot, 'build-profile.json5');
  const localProperties = readFileSync(localPropertiesPath, 'utf8');
  const buildProfile = readFileSync(buildProfilePath, 'utf8');
  const sdkDirMatch = localProperties.match(/^sdk\.dir=(.+)$/m);
  const compileSdkMatch = buildProfile.match(/"compileSdkVersion"\s*:\s*(\d+)/);

  if (!sdkDirMatch || !compileSdkMatch) {
    throw new Error('Unable to resolve sdk.dir or compileSdkVersion for syscap patch task.');
  }

  return resolve(sdkDirMatch[1].trim(), compileSdkMatch[1], 'toolchains', 'syscap_tool');
}

function resolvePackDeviceTypes(modulePath: string): string[] | undefined {
  const packInfoPath = resolve(modulePath, 'build', 'default', 'outputs', 'default', 'pack.info');
  if (!existsSync(packInfoPath)) {
    return undefined;
  }

  const packInfo = JSON.parse(readFileSync(packInfoPath, 'utf8')) as {
    packages?: Array<{ deviceType?: string[] }>;
    summary?: { modules?: Array<{ deviceType?: string[] }> };
  };

  const packageDeviceTypes = packInfo.packages?.find((entry) => Array.isArray(entry.deviceType))?.deviceType;
  if (packageDeviceTypes?.length) {
    return packageDeviceTypes;
  }

  return packInfo.summary?.modules?.find((entry) => Array.isArray(entry.deviceType))?.deviceType;
}

function patchModuleDeviceTypes(filePath: string, deviceTypes: string[]): void {
  if (!existsSync(filePath)) {
    return;
  }

  const moduleJson = JSON.parse(readFileSync(filePath, 'utf8')) as { module?: { deviceTypes?: string[] } };
  if (!moduleJson.module) {
    return;
  }

  moduleJson.module.deviceTypes = deviceTypes;
  writeFileSync(filePath, `${JSON.stringify(moduleJson, null, 2)}\n`);
}

function patchBuiltHapHnpLayout(modulePath: string): void {
  const outputDir = resolve(modulePath, 'build', 'default', 'outputs', 'default');
  if (!existsSync(outputDir)) {
    return;
  }

  const hapFiles = readdirSync(outputDir).filter((entry) => entry.endsWith('.hap'));
  for (const hapFile of hapFiles) {
    const hapPath = resolve(outputDir, hapFile);
    const zipEntries = execFileSync('zipinfo', ['-1', hapPath], { encoding: 'utf8' })
      .split('\n')
      .filter(Boolean);
    const hnpEntries = zipEntries.filter((entry) => entry.startsWith('ets/hnp/') || entry.startsWith('hnp/'));
    if (!hnpEntries.length) {
      continue;
    }

    const tempDir = mkdtempSync(join(tmpdir(), 'niterm-hnp-'));
    try {
      execFileSync('unzip', ['-oq', hapPath, '-d', tempDir], { stdio: 'inherit' });

      const stagedEtsHnpDir = resolve(tempDir, 'ets', 'hnp');
      const stagedHnpDir = resolve(tempDir, 'hnp');
      const stagedAbiHnpDir = resolve(stagedHnpDir, 'arm64-v8a');
      execFileSync('mkdir', ['-p', stagedAbiHnpDir], { stdio: 'inherit' });

      const sourceDirs = [stagedEtsHnpDir, stagedHnpDir];
      for (const sourceDir of sourceDirs) {
        if (!existsSync(sourceDir)) {
          continue;
        }
        for (const entry of readdirSync(sourceDir)) {
          const sourcePath = resolve(sourceDir, entry);
          if (!entry.endsWith('.hnp')) {
            continue;
          }
          if (!statSync(sourcePath).isFile()) {
            continue;
          }
          copyFileSync(sourcePath, resolve(stagedAbiHnpDir, entry));
          rmSync(sourcePath, { force: true });
        }
      }

      rmSync(stagedEtsHnpDir, { recursive: true, force: true });

      const rebuiltHapPath = resolve(tempDir, basename(hapPath));
      execFileSync('zip', ['-qrX', rebuiltHapPath, '.'], { cwd: tempDir, stdio: 'inherit' });
      copyFileSync(rebuiltHapPath, hapPath);
    } finally {
      rmSync(tempDir, { recursive: true, force: true });
    }
  }
}

const rpcidPatchPlugin = {
  pluginId: 'niterm-rpcid-patch',
  apply(node: any) {
    node.registerTask({
      name: 'PatchRpcidFor2in1',
      dependencies: ['default@GeneratePkgModuleJson'],
      postDependencies: ['default@PackageHap'],
      run(taskContext: { modulePath: string }) {
        const syscapDir = resolve(taskContext.modulePath, 'build', 'default', 'intermediates', 'syscap', 'default');
        const rpcidJsonPath = resolve(syscapDir, 'rpcid.json');
        if (!existsSync(rpcidJsonPath)) {
          return;
        }

        const rpcid = JSON.parse(readFileSync(rpcidJsonPath, 'utf8')) as { syscap?: string[] };
        const syscaps = Array.isArray(rpcid.syscap) ? rpcid.syscap : [];
        rpcid.syscap = syscaps.filter((syscap) => !REMOVED_SYSCAPS.has(syscap));
        writeFileSync(rpcidJsonPath, `${JSON.stringify(rpcid, null, 2)}\n`);

        const syscapTool = resolveSyscapTool(taskContext.modulePath);
        if (!existsSync(syscapTool)) {
          throw new Error(`syscap_tool not found at ${syscapTool}`);
        }

        execFileSync(syscapTool, ['-R', '-e', '-i', rpcidJsonPath, '-o', syscapDir], {
          stdio: 'inherit'
        });

        const deviceTypes = resolvePackDeviceTypes(taskContext.modulePath);
        if (!deviceTypes?.length) {
          return;
        }

        patchModuleDeviceTypes(
          resolve(taskContext.modulePath, 'build', 'default', 'intermediates', 'package', 'default', 'module.json'),
          deviceTypes
        );
      }
    });
  }
};

const hnpLayoutPatchPlugin = {
  pluginId: 'niterm-hnp-layout-patch',
  apply(node: any) {
    node.registerTask({
      name: 'PatchBuiltHapHnpLayout',
      dependencies: ['default@PackageHap'],
      postDependencies: ['default@SignHap'],
      run(taskContext: { modulePath: string }) {
        patchBuiltHapHnpLayout(taskContext.modulePath);
      }
    });
  }
};

export default {
  system: hapTasks, /* Built-in plugin of Hvigor. It cannot be modified. */
  plugins: [rpcidPatchPlugin, hnpLayoutPatchPlugin]       /* Custom plugin to extend the functionality of Hvigor. */
}
