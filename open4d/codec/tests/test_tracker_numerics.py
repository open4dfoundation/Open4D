import os
from pathlib import Path
import re
import shutil
import subprocess
from xml.etree import ElementTree

import pytest

pytestmark = pytest.mark.cpu


@pytest.fixture(scope="module", params=("tvmc", "tsmc"))
def tracker_program(request, tmp_path_factory):
    dotnet = os.environ.get("OPEN4D_TEST_DOTNET") or shutil.which("dotnet")
    if not dotnet:
        pytest.skip("a .NET SDK is required for native tracker regressions")
    root = Path(os.environ.get(
        "OPEN4D_TRACKER_TEST_SOURCE_ROOT", Path(__file__).resolve().parents[2] / "codecs"
    ))
    source = root / request.param / "arap-volume-tracking/Framework"
    project = source / "Framework.csproj"
    if not project.is_file():
        pytest.skip(f"{request.param} tracker sources are unavailable")
    target = ElementTree.parse(project).getroot().findtext(".//TargetFramework")
    sdk = subprocess.check_output([dotnet, "--list-sdks"], text=True, timeout=15)
    required = re.fullmatch(r"net(\d+)\.\d+", target or "")
    installed = [int(value) for value in re.findall(r"^(\d+)\.", sdk, re.MULTILINE)]
    if not installed or required and max(installed) < int(required[1]):
        pytest.skip(f"a .NET SDK supporting {target} is required")
    work = tmp_path_factory.mktemp(f"tracker-{request.param}")
    shutil.copytree(source, work / "Framework", ignore=shutil.ignore_patterns("bin", "obj"))
    shutil.copyfile(Path(__file__).with_name("tracker_numerics.cs"), work / "Program.cs")
    (work / "TrackerRegression.csproj").write_text(f"""<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><OutputType>Exe</OutputType><TargetFramework>{target}</TargetFramework>
<EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup>
<ItemGroup><Compile Include="Program.cs" />
<ProjectReference Include="Framework/Framework.csproj" /></ItemGroup></Project>
""")
    built = subprocess.run(
        [dotnet, "build", str(work / "TrackerRegression.csproj"), "--disable-build-servers",
         "--nologo", "--verbosity", "quiet", "--output", str(work / "bin")],
        text=True, capture_output=True, timeout=180,
    )
    assert built.returncode == 0, built.stdout + built.stderr
    return dotnet, work / "bin/TrackerRegression.dll"


@pytest.mark.parametrize("case", (
    "self_distance", "empty_weighted", "zero_weights", "empty_unweighted",
    "nonfinite_kd", "finite_kd",
))
def test_native_tracker_numeric_contracts(tracker_program, case):
    dotnet, program = tracker_program
    result = subprocess.run([dotnet, str(program), case], text=True,
                            capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == f"{case}: passed"
