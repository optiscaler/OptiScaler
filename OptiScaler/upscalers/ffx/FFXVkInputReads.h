#pragma once
namespace FFXVkInputReads
{
template<class Parameters, class Key, class Result, class Resource>
bool Required(Parameters* parameters, Key key, Result success, Resource*& output)
{
    output = nullptr;
    if (parameters->Get(key, reinterpret_cast<void**>(&output)) != success)
    {
        output = nullptr;
        return false;
    }
    return output != nullptr;
}
template<class Parameters, class Key, class Result, class Value>
Value Optional(Parameters* parameters, Key key, Result success, Value fallback)
{
    Value value = fallback;
    return parameters->Get(key, &value) == success ? value : fallback;
}
}
