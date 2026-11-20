#ifndef DESKBUDDY_MATH_ENGINE_H
#define DESKBUDDY_MATH_ENGINE_H

// Exact maths for DeskBuddy, used by the AI through MCP tools.
// Pure C++ with no ESP-IDF dependencies, so it can be unit tested on a PC.
// The firmware is built without C++ exceptions, so every call returns a
// MathResult: ok + text (the answer, or a short error the AI can read out).

#include <string>
#include <vector>

namespace deskbuddy_math {

struct MathResult {
    bool ok;
    std::string text;
};

// Evaluate an expression, e.g. "sqrt(2)*sin(pi/4) + 5!/3 - log2(1024)".
// Operators: + - * / % ^ (power), unary -, postfix ! (factorial), parentheses.
// Implicit multiplication like 2pi or 3(4+1) is supported.
// Constants: pi, e, tau.
// Functions: sqrt cbrt abs exp ln log log2 logb(x,base) pow(x,y)
//            sin cos tan asin acos atan atan2(y,x) (radians)
//            sind cosd tand asind acosd atand (degrees)
//            sinh cosh tanh floor ceil round trunc sign
//            min(...) max(...) hypot(x,y) ncr(n,r) npr(n,r) gcd(a,b) lcm(a,b)
//            sigmoid relu leaky_relu(x[,alpha]) softplus deg(rad) rad(deg)
bool Evaluate(const std::string& expression, double& out, std::string& error);
MathResult Calculate(const std::string& expression);

// Descriptive statistics of a list like "4, 8, 15, 16, 23, 42".
MathResult Statistics(const std::string& numbers);

// Roots of a*x^2 + b*x + c = 0 (real or complex). a = 0 falls back to linear.
MathResult SolveQuadratic(double a, double b, double c);

// Linear system, one equation per row: "2,3,13; 1,-1,-1" means 2x+3y=13, x-y=-1.
// Up to 5 unknowns.
MathResult SolveLinearSystem(const std::string& rows);

// Vector maths for ML: dot, cosine_similarity, euclidean_distance, manhattan_distance,
// norm, softmax, normalize (min-max), standardize (z-score), mse, mae, rmse,
// cross_entropy (a = true probabilities or one-hot, b = predicted probabilities).
MathResult VectorOp(const std::string& operation, const std::string& a, const std::string& b);

// Matrix maths, rows separated by ';' and values by ',' e.g. "1,2;3,4".
// Operations: determinant, inverse, transpose, multiply (a x b), add, subtract. Up to 5x5.
MathResult MatrixOp(const std::string& operation, const std::string& a, const std::string& b);

// Classification metrics from a confusion matrix.
MathResult ClassificationMetrics(int tp, int fp, int fn, int tn);

// Helpers (exposed for tests)
bool ParseList(const std::string& text, std::vector<double>& out, std::string& error);
std::string FormatNumber(double v);

}  // namespace deskbuddy_math

#endif  // DESKBUDDY_MATH_ENGINE_H
