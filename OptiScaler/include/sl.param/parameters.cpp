/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#include <map>
#include <typeinfo>
#include <mutex>

#include <sl.h>
#include "parameters.h"

namespace sl
{

namespace param
{
    
struct Parameter
{      
    template<typename T>
    void operator=(T value) 
    { 
        key = typeid(T).hash_code();  
        if constexpr (std::is_same<T, float>::value) values.f = value;        
        else if constexpr (std::is_same<T, int>::value) values.i = value;        
        else if constexpr (std::is_same<T, unsigned int>::value) values.ui = value;        
        else if constexpr (std::is_same<T, double>::value) values.d = value;
        else if constexpr (std::is_same<T, unsigned long long>::value) values.ull = value;
        else if constexpr (std::is_same<T, void*>::value) values.vp = value;        
        else if constexpr (std::is_same<T, bool>::value) values.b = value;
    }
    
    template<typename T>
    operator T() const 
    { 
        T v = {};        
        if constexpr (std::is_same<T, float>::value)
        {
            if (key == typeid(unsigned long long).hash_code()) v = (T)values.ull;
            else if (key == typeid(float).hash_code()) v = (T)values.f;
            else if (key == typeid(double).hash_code()) v = (T)values.d;
            else if (key == typeid(unsigned int).hash_code()) v = (T)values.ui;
            else if (key == typeid(int).hash_code()) v = (T)values.i;
        }
        else if constexpr (std::is_same<T, int>::value)
        {
            if (key == typeid(unsigned long long).hash_code()) v = (T)values.ull;
            else if (key == typeid(float).hash_code()) v = (T)values.f;
            else if (key == typeid(double).hash_code()) v = (T)values.d;
            else if (key == typeid(int).hash_code()) v = (T)values.i;
            else if (key == typeid(unsigned int).hash_code()) v = (T)values.ui;
        }
        else if constexpr (std::is_same<T, unsigned int>::value)
        {
            if (key == typeid(unsigned long long).hash_code()) v = (T)values.ull;
            else if (key == typeid(float).hash_code()) v = (T)values.f;
            else if (key == typeid(double).hash_code()) v = (T)values.d;
            else if (key == typeid(int).hash_code()) v = (T)values.i;
            else if (key == typeid(unsigned int).hash_code()) v = (T)values.ui;
        }
        else if constexpr (std::is_same<T, double>::value)
        {
            if (key == typeid(unsigned long long).hash_code()) v = (T)values.ull;
            else if (key == typeid(float).hash_code()) v = (T)values.f;
            else if (key == typeid(double).hash_code()) v = (T)values.d;
            else if (key == typeid(int).hash_code()) v = (T)values.i;
            else if (key == typeid(unsigned int).hash_code()) v = (T)values.ui;
        }
        else if constexpr (std::is_same<T, unsigned long long>::value)
        {
            if (key == typeid(unsigned long long).hash_code()) v = (T)values.ull;
            else if (key == typeid(float).hash_code()) v = (T)values.f;
            else if (key == typeid(double).hash_code()) v = (T)values.d;
            else if (key == typeid(int).hash_code()) v = (T)values.i;
            else if (key == typeid(unsigned int).hash_code()) v = (T)values.ui;
            else if (key == typeid(void *).hash_code()) v = (T)values.vp;
        }
        else if constexpr (std::is_same<T, void*>::value)
        {
            if (key == typeid(void *).hash_code()) v = values.vp;
        }
        else if constexpr (std::is_same<T, bool>::value)
        {
            if (key == typeid(bool).hash_code()) v = (T)values.b;
            else if (key == typeid(int).hash_code()) v = (T)values.i != 0;
            else if (key == typeid(unsigned int).hash_code()) v = (T)values.ui != 0;
        }
        return v;
    }
    
    union
    {
        bool b;
        float f;
        double d;
        int i;
        unsigned int ui;
        unsigned long long ull;
        void *vp;
    } values;

    size_t key = 0;
};

struct Parameters : public IParameters
{
    template<typename T>
    void setT(const char* key, T &value)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_values[key] = value;
    }

    void set(const char * key, bool value) override { setT(key, value); }
    void set(const char * key, unsigned long long value) override { setT(key, value);}
    void set(const char * key, float value) override { setT(key, value); }
    void set(const char * key, double value) override { setT(key, value); }
    void set(const char * key, unsigned int value) override { setT(key, value); }
    void set(const char * key, int value) override { setT(key, value); }
    void set(const char * key, void *value) override { setT(key, value); }
    
    template<typename T>
    bool getT(const char* key, T *value) const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        auto k = m_values.find(key);
        if (k == m_values.end()) return false;
        const Parameter &p = (*k).second;
        *value = p;
        return true;
    }

    bool get(const char * key, bool *value) const override { return getT(key, value); }
    bool get(const char * key, unsigned long long *value) const override { return getT(key,value); }
    bool get(const char * key, float *value) const override { return getT(key, value); }
    bool get(const char * key, double *value) const override { return getT(key, value); }
    bool get(const char * key, unsigned int *value) const override { return getT(key, value); }
    bool get(const char * key, int *value) const override { return getT(key, value); }
    bool get(const char * key, void **value) const override { return getT(key, value); }

    std::vector<std::string> enumerate() const override
    {
        std::vector<std::string> keys;
        for (auto& value : m_values)
        {
            keys.push_back(value.first);
        }
        return keys;
    }

    inline static Parameters* s_params = {};

private:
    std::map<std::string, Parameter> m_values;
    mutable std::mutex m_mutex;
};

IParameters *getInterface() 
{ 
    if (!Parameters::s_params)
    {
        Parameters::s_params = new Parameters();
    }
    return Parameters::s_params;
}

void destroyInterface()
{
    delete Parameters::s_params;
    Parameters::s_params = {};
}

} // namespace api
} // namespace sl
