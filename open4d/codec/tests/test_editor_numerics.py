import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

pytestmark = pytest.mark.cpu


@pytest.fixture(scope="module", params=("tvmc", "tsmc"))
def editor_program(request, tmp_path_factory):
    dotnet = os.environ.get("OPEN4D_TEST_DOTNET") or shutil.which("dotnet")
    if not dotnet:
        pytest.skip("a .NET SDK is required for native editor regressions")
    root = Path(os.environ.get(
        "OPEN4D_EDITOR_TEST_SOURCE_ROOT", Path(__file__).resolve().parents[2] / "codecs"
    ))
    source = root / request.param / "tvm-editing/TVMEditor"
    if not (source / "TVMEditor.csproj").is_file():
        pytest.skip(f"{request.param} editor sources are unavailable")
    sdk = subprocess.check_output([dotnet, "--list-sdks"], text=True, timeout=15)
    installed = [int(value) for value in re.findall(r"^(\d+)\.", sdk, re.MULTILINE)]
    if not installed or max(installed) < 3:
        pytest.skip("a .NET SDK supporting netstandard2.1 is required")
    work = tmp_path_factory.mktemp(f"editor-{request.param}")
    shutil.copytree(source, work / "TVMEditor", ignore=shutil.ignore_patterns("bin", "obj"))
    shutil.copyfile(Path(__file__).with_name("editor_numerics.cs"), work / "Program.cs")
    (work / "EditorRegression.csproj").write_text(f"""<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net{max(installed)}.0</TargetFramework>
<EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup>
<ItemGroup><Compile Include="Program.cs" />
<ProjectReference Include="TVMEditor/TVMEditor.csproj" /></ItemGroup></Project>
""")
    built = subprocess.run(
        [dotnet, "build", str(work / "EditorRegression.csproj"), "--disable-build-servers",
         "--nologo", "--verbosity", "quiet", "--output", str(work / "bin")],
        text=True, capture_output=True, timeout=180,
    )
    assert built.returncode == 0, built.stdout + built.stderr
    return dotnet, work / "bin/EditorRegression.dll"


@pytest.mark.parametrize("case", (
    "zero_affinity", "few_centers", "duplicate_centers", "empty_centers", "finite_deformation",
    "partial_effectors", "empty_effectors", "invalid_affinity",
))
def test_native_editor_numeric_contracts(editor_program, case):
    dotnet, program = editor_program
    result = subprocess.run([dotnet, str(program), case], text=True,
                            capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip() == f"{case}: passed"
