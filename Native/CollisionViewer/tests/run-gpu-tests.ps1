param([string]$RuntimeDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('DEViL-GPU-'+[Guid]::NewGuid().ToString('N'))))
# Run hidden, isolated Vulkan processes. The game and its registration are unchanged.
if($RuntimeDirectory -match '[^\x00-\x7f]') { throw 'Supply -RuntimeDirectory with an ASCII path for the Vulkan loader.' }
New-Item -ItemType Directory -Force (Join-Path (Split-Path -Parent $PSScriptRoot) 'build') | Out-Null
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$bin=Join-Path $projectRoot 'bin'
$results=@()
foreach($case in @('hidden','partial','front','reverse','inverted','inverted-partial','inverted-front','console-half','console-full','concurrent-hidden','concurrent-partial','concurrent-front','concurrent-console-half','concurrent-console-full','exclusive-hidden','exclusive-partial','exclusive-front','exclusive-console-half','exclusive-console-full')) {
    $baseCase=$case; $split=''
    if($case.StartsWith('concurrent-')){$split='concurrent';$baseCase=$case.Substring(11)}
    if($case.StartsWith('exclusive-')){$split='exclusive';$baseCase=$case.Substring(10)}
    $runtime=Join-Path $RuntimeDirectory $case
    New-Item -ItemType Directory -Force -Path $runtime | Out-Null
    Copy-Item -LiteralPath (Join-Path $bin 'DoomCollisionLayer.dll') -Destination $runtime -Force
    Copy-Item -LiteralPath (Join-Path $bin 'VulkanSmoke.exe') -Destination $runtime -Force
    $manifest=[ordered]@{file_format_version='1.2.0';layer=[ordered]@{name='VK_LAYER_DOOM_collision';type='GLOBAL';library_path='.\DoomCollisionLayer.dll';api_version='1.3.0';implementation_version='1';description='DEViL isolated GPU test';functions=@{vkNegotiateLoaderLayerInterfaceVersion='DoomNegotiateLayerInterfaceVersion'}}}
    [IO.File]::WriteAllText((Join-Path $runtime 'DoomCollisionLayer.json'),($manifest|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=Join-Path $runtime 'VulkanSmoke.exe'
    $info.WorkingDirectory=$runtime
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $info.Environment['VK_LAYER_PATH']=$runtime
    $info.Environment['VK_INSTANCE_LAYERS']='VK_LAYER_DOOM_collision'
    $info.Environment['DEVIL_DISABLE_COLLISION_OVERRIDE']='1'
    $info.Environment['VK_LOADER_DEBUG']='error,warn'
    $info.Environment['DISABLE_VK_LAYER_reshade_collision_viewer']='1'
    $info.Environment['DISABLE_VK_LAYER_reshade']='1'
    $info.Environment['DCV_SMOKE_TEST']='1'
    if($split){$info.Environment['DCV_TEST_SPLIT_PRESENT']=$split}else{$info.Environment.Remove('DCV_TEST_SPLIT_PRESENT') | Out-Null}
    if($baseCase.StartsWith('console-')) {
        $info.Environment.Remove('DCV_OCCLUSION_TEST') | Out-Null
        $info.Environment['DCV_TEST_CONSOLE_FRACTION']=if($baseCase -eq 'console-half'){'0.5'}else{'1'}
    } else {
        $info.Environment['DCV_OCCLUSION_TEST']=$baseCase
        $info.Environment['DCV_TEST_CONSOLE_FRACTION']='0'
    }
    $p=[Diagnostics.Process]::Start($info)
    $stdout=$p.StandardOutput.ReadToEndAsync(); $stderr=$p.StandardError.ReadToEndAsync()
    if(-not $p.WaitForExit(15000)){$p.Kill();throw ('Isolated GPU test timed out: '+$case)}
    [IO.File]::WriteAllText((Join-Path $runtime 'stdout.txt'),$stdout.Result)
    [IO.File]::WriteAllText((Join-Path $runtime 'stderr.txt'),$stderr.Result)
    if($p.ExitCode -ne 0){throw ($case+' failed: '+$stderr.Result)}
    $result=Get-Content -LiteralPath (Join-Path $runtime 'smoke-result.json') | ConvertFrom-Json
    if(-not $result.rendered){throw ('GPU readback failed: '+$case)}
    if($baseCase.StartsWith('console-') -and $result.consoleLeakPixels -ne 0){throw ('Overlay leaked into console: '+$case)}
    $results+=[pscustomobject]@{Case=$case;Passed=$result.rendered;GreenPixels=$result.greenPixels;CyanPixels=$result.cyanPixels;ConsoleLeakPixels=$result.consoleLeakPixels}
    Write-Output ('PASS: '+$case)
}
[IO.File]::WriteAllText((Join-Path $projectRoot 'build\gpu-verification.json'),($results|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
