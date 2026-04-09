module;

#include <cstdio>

export module subprocess:pipe_var;

import std;
import :basic_types;
import :pipe;

export namespace subprocess
{

    enum class PipeVarIndex
    {
        option,
        string,
        handle,
        istream,
        ostream,
        file
    };

    typedef std::variant<PipeOption, std::string, PipeHandle, std::istream *, std::ostream *, FILE *> PipeVar;

    PipeOption get_pipe_option(const PipeVar &option);

} // namespace subprocess

namespace subprocess
{

    PipeOption get_pipe_option(const PipeVar &option)
    {
        PipeVarIndex index = static_cast<PipeVarIndex>(option.index());

        switch (index)
        {
        case PipeVarIndex::option:
            return std::get<PipeOption>(option);
        case PipeVarIndex::handle:
            return PipeOption::specific;
        default:
            return PipeOption::pipe;
        }
    }

} // namespace subprocess
