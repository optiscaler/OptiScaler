#pragma once

// CPU seam identity only. It is never evidence that GPU work has completed.
struct DlssNrResidualPair
{
    const void* command = nullptr;
    const void* parameters = nullptr;
    const void* output = nullptr;

    void Cancel() { command = parameters = output = nullptr; }
    void Arm(const void* cmd, const void* params, const void* out)
    {
        command = cmd;
        parameters = params;
        output = out;
    }
    bool Take(const void* cmd, const void* params, const void* out)
    {
        const bool matches = command && output && command == cmd && parameters == params && output == out;
        Cancel();
        return matches;
    }
};
