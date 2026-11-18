"""Exercise the actual patched HookToDevice decision block with a fake host.
Usage: python3 check_rdr2_startup.py /path/to/patched/OptiScaler
Requires g++; does not simulate Vulkan, Streamline binaries, or GPU execution.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1])
source = (root / 'OptiScaler/hooks/D3D12_Hooks.cpp').read_text(encoding='utf-8-sig')
start = source.index('    if (State::Instance().activeFgOutput == FGOutput::DLSSG && !State::Instance().creatingD3DDevice &&')
end = source.index('\nstatic void UnhookDevice()', start)
block = source[start:end].rstrip()
assert block.endswith('}')
block = block[:-1]  # remove the enclosing HookToDevice's closing brace
harness = r'''
#include <atomic>
#include <cassert>
#include <string>
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
enum class FGOutput { DLSSG, Other };
enum class FGNvngxReplacement { Custom, None };
namespace GameQuirk { constexpr unsigned DeferStreamlineUntilRenderApi = 1; }
struct State {
 FGOutput activeFgOutput = FGOutput::DLSSG;
 FGNvngxReplacement activeFgNvngx = FGNvngxReplacement::Custom;
 bool creatingD3DDevice = false;
 unsigned gameQuirks = GameQuirk::DeferStreamlineUntilRenderApi;
 std::string activeFgNvngxCustom = "XeMFG XMX";
 static State& Instance() { static State s; return s; }
};
int dxInits = 0, preloads = 0;
bool recurse = false, loadWorks = true;
void Hook(void* InDevice);
namespace StreamlineProxy {
 bool LoadStreamline() { return loadWorks; }
 bool InitWithD3D12(void*) { ++dxInits; return true; }
}
namespace CustomProviders {
 enum class Kind { Dlssg };
 bool Load(const std::string& name, Kind) {
  assert(name == "XeMFG XMX"); ++preloads;
  if (recurse) Hook(nullptr);
  return true;
 }
}
void Hook(void* InDevice) {
''' + block + r'''
}
int main(int argc, char** argv) {
 assert(argc == 2);
 const std::string mode = argv[1];
 auto& state = State::Instance();
 if (mode == "other-game") state.gameQuirks = 0;
 if (mode == "internal-device") state.creatingD3DDevice = true;
 if (mode == "other-output") state.activeFgOutput = FGOutput::Other;
 if (mode == "native-dlssg") state.activeFgNvngx = FGNvngxReplacement::None;
 if (mode == "load-failed") loadWorks = false;
 if (mode == "recursive-provider") recurse = true;
 Hook(nullptr);
 Hook(nullptr);
 if (mode == "other-game") { assert(dxInits == 2); assert(preloads == 0); }
 else {
  assert(dxInits == 0);
  assert(preloads == ((mode == "rdr2-probe" || mode == "recursive-provider") ? 1 : 0));
 }
}
'''
with tempfile.TemporaryDirectory() as folder:
    src = Path(folder) / 'startup.cpp'
    exe = Path(folder) / 'startup'
    src.write_text(harness)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', str(src), '-o', str(exe)], check=True)
    for mode in ['rdr2-probe', 'other-game', 'internal-device', 'other-output', 'native-dlssg', 'load-failed', 'recursive-provider']:
        subprocess.run([str(exe), mode], check=True, timeout=5)
        print('PASS', mode)
