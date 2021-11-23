#include "stmdsp.hpp"
#include "exprtk.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

std::vector<stmdsp::dacsample_t> deviceGenLoadFormulaEval(const std::string& formulaString)
{
    double x = 0;

    exprtk::symbol_table<double> symbol_table;
    exprtk::expression<double> expression;
    exprtk::parser<double> parser;

    symbol_table.add_variable("x", x);
    symbol_table.add_constants();
    expression.register_symbol_table(symbol_table);
    parser.compile(formulaString, expression);

    std::vector<stmdsp::dacsample_t> samples (stmdsp::SAMPLES_MAX);

    auto genFun = [&x, &expression] {
        stmdsp::dacsample_t s = expression.value();
        ++x;
        return s;
    };

    std::generate(samples.begin(), samples.end(), genFun);
    return samples;
}

