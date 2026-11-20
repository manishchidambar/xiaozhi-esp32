#include "math_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace deskbuddy_math {

namespace {

constexpr int kMaxDepth = 64;      // recursion guard for nested parentheses
constexpr int kMaxDim = 5;         // largest matrix / linear system
constexpr size_t kMaxList = 200;   // longest number list
constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;

MathResult Ok(const std::string& s) { return {true, s}; }
MathResult Err(const std::string& s) { return {false, "Error: " + s}; }

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// ------------------------------------------------------------------ parser
class Parser {
public:
    explicit Parser(const std::string& s) : s_(Lower(s)) {}

    bool Run(double& out, std::string& error) {
        double v = Expr();
        SkipSpaces();
        if (err_.empty() && pos_ < s_.size()) {
            Fail(std::string("unexpected '") + s_[pos_] + "'");
        }
        if (!err_.empty()) {
            error = err_;
            return false;
        }
        if (std::isnan(v)) {
            error = "result is not a number (check the domain, e.g. sqrt of a negative)";
            return false;
        }
        if (std::isinf(v)) {
            error = "result is infinite (division by zero or overflow)";
            return false;
        }
        out = v;
        return true;
    }

private:
    std::string s_;
    size_t pos_ = 0;
    int depth_ = 0;
    std::string err_;

    double Fail(const std::string& m) {
        if (err_.empty()) err_ = m;
        return 0;
    }

    void SkipSpaces() {
        while (pos_ < s_.size() && std::isspace((unsigned char)s_[pos_])) pos_++;
    }

    bool Eat(char c) {
        SkipSpaces();
        if (pos_ < s_.size() && s_[pos_] == c) {
            pos_++;
            return true;
        }
        return false;
    }

    char Peek() {
        SkipSpaces();
        return pos_ < s_.size() ? s_[pos_] : '\0';
    }

    // expr := term (('+'|'-') term)*
    double Expr() {
        if (++depth_ > kMaxDepth) return Fail("expression nested too deeply");
        double v = Term();
        while (err_.empty()) {
            if (Eat('+')) v += Term();
            else if (Eat('-')) v -= Term();
            else break;
        }
        depth_--;
        return v;
    }

    // term := unary (('*'|'/'|'%'|implicit) unary)*
    double Term() {
        double v = Unary();
        while (err_.empty()) {
            char c = Peek();
            if (c == '*') {
                pos_++;
                v *= Unary();
            } else if (c == '/') {
                pos_++;
                double d = Unary();
                if (d == 0) return Fail("division by zero");
                v /= d;
            } else if (c == '%') {
                pos_++;
                double d = Unary();
                if (d == 0) return Fail("modulo by zero");
                v = std::fmod(v, d);
            } else if (c == '(' || std::isalpha((unsigned char)c)) {
                // implicit multiplication: 2pi, 3(4+1), 2sqrt(2)
                v *= Unary();
            } else {
                break;
            }
        }
        return v;
    }

    // unary := ('-'|'+') unary | power
    double Unary() {
        if (Eat('-')) return -Unary();
        if (Eat('+')) return Unary();
        return Power();
    }

    // power := postfix ('^' unary)?   (right associative, -2^2 = -4)
    double Power() {
        double base = Postfix();
        if (Eat('^')) {
            double ex = Unary();
            return std::pow(base, ex);
        }
        return base;
    }

    // postfix := primary '!'*
    double Postfix() {
        double v = Primary();
        while (err_.empty() && Peek() == '!') {
            pos_++;
            if (v < 0 || v != std::floor(v)) return Fail("factorial needs a whole number >= 0");
            if (v > 170) return Fail("factorial too large (max 170!)");
            double r = 1;
            for (int i = 2; i <= (int)v; i++) r *= i;
            v = r;
        }
        return v;
    }

    double Primary() {
        SkipSpaces();
        if (pos_ >= s_.size()) return Fail("expression ended early");
        char c = s_[pos_];
        if (c == '(') {
            pos_++;
            double v = Expr();
            if (!Eat(')')) return Fail("missing ')'");
            return v;
        }
        if (std::isdigit((unsigned char)c) || c == '.') {
            const char* start = s_.c_str() + pos_;
            char* end = nullptr;
            double v = std::strtod(start, &end);
            if (end == start) return Fail("bad number");
            pos_ += (size_t)(end - start);
            return v;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t st = pos_;
            while (pos_ < s_.size() &&
                   (std::isalnum((unsigned char)s_[pos_]) || s_[pos_] == '_')) {
                pos_++;
            }
            std::string name = s_.substr(st, pos_ - st);
            if (Peek() == '(') {
                pos_++;
                std::vector<double> args;
                if (!Eat(')')) {
                    do {
                        args.push_back(Expr());
                        if (!err_.empty()) return 0;
                    } while (Eat(','));
                    if (!Eat(')')) return Fail("missing ')' after arguments of " + name);
                }
                return Call(name, args);
            }
            if (name == "pi") return kPi;
            if (name == "e") return kE;
            if (name == "tau") return 2 * kPi;
            return Fail("unknown name '" + name + "'");
        }
        return Fail(std::string("unexpected '") + c + "'");
    }

    static double Gcd(double a, double b) {
        a = std::fabs(a);
        b = std::fabs(b);
        while (b > 0.5) {
            double t = std::fmod(a, b);
            a = b;
            b = t;
        }
        return a;
    }

    double Call(const std::string& f, const std::vector<double>& a) {
        auto need = [&](size_t n) -> bool {
            if (a.size() != n) {
                Fail(f + " needs " + std::to_string(n) + " value" + (n == 1 ? "" : "s"));
                return false;
            }
            return true;
        };
        const double d2r = kPi / 180.0;
        if (f == "sqrt") { if (!need(1)) return 0; if (a[0] < 0) return Fail("sqrt of a negative number"); return std::sqrt(a[0]); }
        if (f == "cbrt") { if (!need(1)) return 0; return std::cbrt(a[0]); }
        if (f == "abs") { if (!need(1)) return 0; return std::fabs(a[0]); }
        if (f == "exp") { if (!need(1)) return 0; return std::exp(a[0]); }
        if (f == "ln") { if (!need(1)) return 0; if (a[0] <= 0) return Fail("ln needs a positive number"); return std::log(a[0]); }
        if (f == "log" || f == "log10") {
            if (a.size() == 2) {
                if (a[0] <= 0 || a[1] <= 0 || a[1] == 1) return Fail("bad log arguments");
                return std::log(a[0]) / std::log(a[1]);
            }
            if (!need(1)) return 0;
            if (a[0] <= 0) return Fail("log needs a positive number");
            return std::log10(a[0]);
        }
        if (f == "log2") { if (!need(1)) return 0; if (a[0] <= 0) return Fail("log2 needs a positive number"); return std::log2(a[0]); }
        if (f == "logb") { if (!need(2)) return 0; if (a[0] <= 0 || a[1] <= 0 || a[1] == 1) return Fail("bad log arguments"); return std::log(a[0]) / std::log(a[1]); }
        if (f == "pow") { if (!need(2)) return 0; return std::pow(a[0], a[1]); }
        if (f == "sin") { if (!need(1)) return 0; return std::sin(a[0]); }
        if (f == "cos") { if (!need(1)) return 0; return std::cos(a[0]); }
        if (f == "tan") { if (!need(1)) return 0; return std::tan(a[0]); }
        if (f == "asin") { if (!need(1)) return 0; return std::asin(a[0]); }
        if (f == "acos") { if (!need(1)) return 0; return std::acos(a[0]); }
        if (f == "atan") { if (!need(1)) return 0; return std::atan(a[0]); }
        if (f == "atan2") { if (!need(2)) return 0; return std::atan2(a[0], a[1]); }
        if (f == "sind") { if (!need(1)) return 0; return Clean(std::sin(a[0] * d2r)); }
        if (f == "cosd") { if (!need(1)) return 0; return Clean(std::cos(a[0] * d2r)); }
        if (f == "tand") {
            if (!need(1)) return 0;
            double m = std::fmod(std::fabs(a[0]), 180.0);
            if (std::fabs(m - 90.0) < 1e-9) return Fail("tan is undefined at 90 degrees");
            return Clean(std::tan(a[0] * d2r));
        }
        if (f == "asind") { if (!need(1)) return 0; return std::asin(a[0]) / d2r; }
        if (f == "acosd") { if (!need(1)) return 0; return std::acos(a[0]) / d2r; }
        if (f == "atand") { if (!need(1)) return 0; return std::atan(a[0]) / d2r; }
        if (f == "sinh") { if (!need(1)) return 0; return std::sinh(a[0]); }
        if (f == "cosh") { if (!need(1)) return 0; return std::cosh(a[0]); }
        if (f == "tanh") { if (!need(1)) return 0; return std::tanh(a[0]); }
        if (f == "floor") { if (!need(1)) return 0; return std::floor(a[0]); }
        if (f == "ceil") { if (!need(1)) return 0; return std::ceil(a[0]); }
        if (f == "round") {
            if (a.size() == 2) {
                double p = std::pow(10.0, a[1]);
                return std::round(a[0] * p) / p;
            }
            if (!need(1)) return 0;
            return std::round(a[0]);
        }
        if (f == "trunc") { if (!need(1)) return 0; return std::trunc(a[0]); }
        if (f == "sign") { if (!need(1)) return 0; return (a[0] > 0) - (a[0] < 0); }
        if (f == "min" || f == "max") {
            if (a.empty()) return Fail(f + " needs at least one value");
            return f == "min" ? *std::min_element(a.begin(), a.end()) : *std::max_element(a.begin(), a.end());
        }
        if (f == "hypot") { if (!need(2)) return 0; return std::hypot(a[0], a[1]); }
        if (f == "ncr" || f == "npr") {
            if (!need(2)) return 0;
            double n = a[0], r = a[1];
            if (n < 0 || r < 0 || n != std::floor(n) || r != std::floor(r) || r > n) {
                return Fail(f + " needs whole numbers with 0 <= r <= n");
            }
            double res = 1;
            if (f == "npr") {
                for (int i = 0; i < (int)r; i++) res *= (n - i);
            } else {
                double k = std::min(r, n - r);
                for (int i = 1; i <= (int)k; i++) res = res * (n - k + i) / i;
                res = std::round(res);
            }
            return res;
        }
        if (f == "gcd") { if (!need(2)) return 0; return Gcd(std::round(a[0]), std::round(a[1])); }
        if (f == "lcm") {
            if (!need(2)) return 0;
            double g = Gcd(std::round(a[0]), std::round(a[1]));
            if (g == 0) return 0;
            return std::fabs(std::round(a[0]) / g * std::round(a[1]));
        }
        if (f == "sigmoid") { if (!need(1)) return 0; return 1.0 / (1.0 + std::exp(-a[0])); }
        if (f == "relu") { if (!need(1)) return 0; return a[0] > 0 ? a[0] : 0; }
        if (f == "leaky_relu") {
            if (a.size() != 1 && a.size() != 2) return Fail("leaky_relu needs x and optional alpha");
            double al = a.size() == 2 ? a[1] : 0.01;
            return a[0] > 0 ? a[0] : al * a[0];
        }
        if (f == "softplus") { if (!need(1)) return 0; return std::log1p(std::exp(a[0])); }
        if (f == "deg") { if (!need(1)) return 0; return a[0] / d2r; }
        if (f == "rad") { if (!need(1)) return 0; return a[0] * d2r; }
        return Fail("unknown function '" + f + "'");
    }

    // sin(180 degrees) should read as 0, not 1.2e-16
    static double Clean(double v) { return std::fabs(v) < 1e-12 ? 0.0 : v; }
};

// ------------------------------------------------------------------ helpers
bool ParseMatrix(const std::string& text, std::vector<std::vector<double>>& m, std::string& error) {
    m.clear();
    size_t start = 0;
    std::string s = text;
    while (start <= s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string::npos) end = s.size();
        std::string row = Trim(s.substr(start, end - start));
        if (!row.empty()) {
            std::vector<double> r;
            if (!ParseList(row, r, error)) return false;
            m.push_back(r);
        }
        start = end + 1;
    }
    if (m.empty()) {
        error = "empty matrix";
        return false;
    }
    for (auto& r : m) {
        if (r.size() != m[0].size()) {
            error = "every row needs the same number of values";
            return false;
        }
    }
    if ((int)m.size() > kMaxDim || (int)m[0].size() > kMaxDim + 1) {
        error = "matrix too large (max 5x5)";
        return false;
    }
    return true;
}

std::string FormatMatrix(const std::vector<std::vector<double>>& m) {
    std::string out;
    for (size_t i = 0; i < m.size(); i++) {
        if (i) out += "; ";
        for (size_t j = 0; j < m[i].size(); j++) {
            if (j) out += ", ";
            out += FormatNumber(m[i][j]);
        }
    }
    return out;
}

std::string FormatList(const std::vector<double>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) out += ", ";
        out += FormatNumber(v[i]);
    }
    return out;
}

// Gaussian elimination with partial pivoting on an augmented or square matrix.
// Returns the determinant of the left square part; fills inverse/solution if asked.
double GaussJordan(std::vector<std::vector<double>> a, int n, int extra_cols,
                   std::vector<std::vector<double>>* result) {
    double det = 1;
    int cols = n + extra_cols;
    for (int c = 0; c < n; c++) {
        int piv = c;
        for (int r = c + 1; r < n; r++) {
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        }
        if (std::fabs(a[piv][c]) < 1e-12) return 0;
        if (piv != c) {
            std::swap(a[piv], a[c]);
            det = -det;
        }
        double p = a[c][c];
        det *= p;
        for (int j = 0; j < cols; j++) a[c][j] /= p;
        for (int r = 0; r < n; r++) {
            if (r == c) continue;
            double f = a[r][c];
            if (f == 0) continue;
            for (int j = 0; j < cols; j++) a[r][j] -= f * a[c][j];
        }
    }
    if (result) {
        result->assign(n, std::vector<double>(extra_cols));
        for (int r = 0; r < n; r++) {
            for (int j = 0; j < extra_cols; j++) (*result)[r][j] = a[r][n + j];
        }
    }
    return det;
}

}  // namespace

// ------------------------------------------------------------------ public
std::string FormatNumber(double v) {
    if (std::fabs(v) < 1e-12) v = 0;
    char buf[40];
    double r = std::round(v);
    if (std::fabs(v - r) < 1e-9 * std::max(1.0, std::fabs(v)) && std::fabs(r) < 1e15) {
        snprintf(buf, sizeof(buf), "%.0f", r);
    } else {
        snprintf(buf, sizeof(buf), "%.10g", v);
    }
    return buf;
}

bool ParseList(const std::string& text, std::vector<double>& out, std::string& error) {
    out.clear();
    std::string cur;
    std::string s = text + ",";
    for (char c : s) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\n') {
            std::string t = Trim(cur);
            cur.clear();
            if (t.empty()) continue;
            double v;
            std::string e;
            if (!Evaluate(t, v, e)) {
                error = "could not read '" + t + "': " + e;
                return false;
            }
            out.push_back(v);
            if (out.size() > kMaxList) {
                error = "too many numbers (max 200)";
                return false;
            }
        } else {
            cur += c;
        }
    }
    if (out.empty()) {
        error = "no numbers given";
        return false;
    }
    return true;
}

bool Evaluate(const std::string& expression, double& out, std::string& error) {
    if (Trim(expression).empty()) {
        error = "empty expression";
        return false;
    }
    if (expression.size() > 300) {
        error = "expression too long";
        return false;
    }
    Parser p(expression);
    return p.Run(out, error);
}

MathResult Calculate(const std::string& expression) {
    double v;
    std::string e;
    if (!Evaluate(expression, v, e)) return Err(e);
    return Ok(Trim(expression) + " = " + FormatNumber(v));
}

MathResult Statistics(const std::string& numbers) {
    std::vector<double> v;
    std::string e;
    if (!ParseList(numbers, v, e)) return Err(e);
    size_t n = v.size();
    double sum = 0;
    for (double x : v) sum += x;
    double mean = sum / n;
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    double median = (n % 2) ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2.0;
    double ss = 0;
    for (double x : v) ss += (x - mean) * (x - mean);
    double var_p = ss / n;
    double var_s = n > 1 ? ss / (n - 1) : 0;

    // mode (only reported if some value repeats)
    std::map<double, int> count;
    int best = 1;
    for (double x : v) best = std::max(best, ++count[x]);
    std::string mode = "none";
    if (best > 1) {
        mode.clear();
        for (auto& kv : count) {
            if (kv.second == best) {
                if (!mode.empty()) mode += ", ";
                mode += FormatNumber(kv.first);
            }
        }
    }
    // quartiles (linear interpolation, same as numpy default)
    auto pct = [&](double q) {
        double pos = q * (n - 1);
        size_t lo = (size_t)std::floor(pos);
        size_t hi = std::min(lo + 1, n - 1);
        return s[lo] + (s[hi] - s[lo]) * (pos - lo);
    };
    // Built step by step: one long chain of + creates many temporaries on the stack,
    // and the firmware fails the build above 1536 bytes of stack per function.
    std::string out;
    out.reserve(320);
    out += "count ";
    out += std::to_string(n);
    out += ", sum ";
    out += FormatNumber(sum);
    out += ", mean ";
    out += FormatNumber(mean);
    out += ", median ";
    out += FormatNumber(median);
    out += ", mode ";
    out += mode;
    out += ", min ";
    out += FormatNumber(s.front());
    out += ", max ";
    out += FormatNumber(s.back());
    out += ", range ";
    out += FormatNumber(s.back() - s.front());
    out += ", Q1 ";
    out += FormatNumber(pct(0.25));
    out += ", Q3 ";
    out += FormatNumber(pct(0.75));
    out += ", population variance ";
    out += FormatNumber(var_p);
    out += ", population std dev ";
    out += FormatNumber(std::sqrt(var_p));
    if (n > 1) {
        out += ", sample variance ";
        out += FormatNumber(var_s);
        out += ", sample std dev ";
        out += FormatNumber(std::sqrt(var_s));
    }
    return Ok(out);
}

MathResult SolveQuadratic(double a, double b, double c) {
    if (a == 0) {
        if (b == 0) return Err(c == 0 ? "every x is a solution" : "no solution");
        return Ok("linear equation, x = " + FormatNumber(-c / b));
    }
    double disc = b * b - 4 * a * c;
    std::string d = "discriminant " + FormatNumber(disc) + ". ";
    if (std::fabs(disc) < 1e-12) {
        return Ok(d + "One repeated root: x = " + FormatNumber(-b / (2 * a)));
    }
    if (disc > 0) {
        double q = -0.5 * (b + (b >= 0 ? 1 : -1) * std::sqrt(disc));  // stable form
        double x1 = q / a;
        double x2 = c / q;
        if (x1 > x2) std::swap(x1, x2);
        return Ok(d + "Two real roots: x = " + FormatNumber(x1) + " and x = " + FormatNumber(x2));
    }
    double re = -b / (2 * a);
    double im = std::sqrt(-disc) / (2 * std::fabs(a));
    return Ok(d + "Two complex roots: x = " + FormatNumber(re) + " + " + FormatNumber(im) +
              "i and x = " + FormatNumber(re) + " - " + FormatNumber(im) + "i");
}

MathResult SolveLinearSystem(const std::string& rows) {
    std::vector<std::vector<double>> m;
    std::string e;
    if (!ParseMatrix(rows, m, e)) return Err(e);
    int n = (int)m.size();
    if ((int)m[0].size() != n + 1) {
        return Err("each row needs " + std::to_string(n) +
                   " coefficients and the right-hand side (n unknowns need n equations)");
    }
    std::vector<std::vector<double>> sol;
    double det = GaussJordan(m, n, 1, &sol);
    if (std::fabs(det) < 1e-12) return Err("no unique solution (the equations are dependent or inconsistent)");
    static const char* names[] = {"x", "y", "z", "w", "v"};
    std::string out;
    for (int i = 0; i < n; i++) {
        if (i) out += ", ";
        out += std::string(names[i]) + " = " + FormatNumber(sol[i][0]);
    }
    return Ok(out);
}

MathResult VectorOp(const std::string& operation, const std::string& a, const std::string& b) {
    std::string op = Lower(Trim(operation));
    std::vector<double> x, y;
    std::string e;
    if (!ParseList(a, x, e)) return Err("vector a: " + e);
    bool needs_b = op == "dot" || op == "cosine_similarity" || op == "euclidean_distance" ||
                   op == "manhattan_distance" || op == "mse" || op == "mae" || op == "rmse" ||
                   op == "cross_entropy";
    if (needs_b) {
        if (!ParseList(b, y, e)) return Err("vector b: " + e);
        if (x.size() != y.size()) return Err("vectors a and b must have the same length");
    }
    size_t n = x.size();
    if (op == "dot") {
        double s = 0;
        for (size_t i = 0; i < n; i++) s += x[i] * y[i];
        return Ok("dot product = " + FormatNumber(s));
    }
    if (op == "norm") {
        double s = 0;
        for (double v : x) s += v * v;
        return Ok("L2 norm = " + FormatNumber(std::sqrt(s)));
    }
    if (op == "cosine_similarity") {
        double d = 0, na = 0, nb = 0;
        for (size_t i = 0; i < n; i++) {
            d += x[i] * y[i];
            na += x[i] * x[i];
            nb += y[i] * y[i];
        }
        if (na == 0 || nb == 0) return Err("cosine similarity is undefined for a zero vector");
        return Ok("cosine similarity = " + FormatNumber(d / std::sqrt(na * nb)));
    }
    if (op == "euclidean_distance" || op == "manhattan_distance") {
        double s = 0;
        for (size_t i = 0; i < n; i++) {
            double d = x[i] - y[i];
            s += op[0] == 'e' ? d * d : std::fabs(d);
        }
        return Ok(op == "euclidean_distance" ? "euclidean distance = " + FormatNumber(std::sqrt(s))
                                             : "manhattan distance = " + FormatNumber(s));
    }
    if (op == "softmax") {
        double mx = *std::max_element(x.begin(), x.end());
        std::vector<double> out(n);
        double s = 0;
        for (size_t i = 0; i < n; i++) s += (out[i] = std::exp(x[i] - mx));
        for (auto& v : out) v /= s;
        return Ok("softmax = " + FormatList(out));
    }
    if (op == "normalize") {
        double lo = *std::min_element(x.begin(), x.end());
        double hi = *std::max_element(x.begin(), x.end());
        if (hi == lo) return Err("all values are equal, cannot min-max normalize");
        std::vector<double> out(n);
        for (size_t i = 0; i < n; i++) out[i] = (x[i] - lo) / (hi - lo);
        return Ok("min-max normalized = " + FormatList(out));
    }
    if (op == "standardize") {
        double m = 0;
        for (double v : x) m += v;
        m /= n;
        double ss = 0;
        for (double v : x) ss += (v - m) * (v - m);
        double sd = std::sqrt(ss / n);
        if (sd == 0) return Err("standard deviation is zero, cannot standardize");
        std::vector<double> out(n);
        for (size_t i = 0; i < n; i++) out[i] = (x[i] - m) / sd;
        return Ok("z-scores (population std) = " + FormatList(out));
    }
    if (op == "mse" || op == "mae" || op == "rmse") {
        double s = 0;
        for (size_t i = 0; i < n; i++) {
            double d = x[i] - y[i];
            s += op == "mae" ? std::fabs(d) : d * d;
        }
        s /= n;
        if (op == "rmse") return Ok("RMSE = " + FormatNumber(std::sqrt(s)));
        return Ok((op == "mse" ? "MSE = " : "MAE = ") + FormatNumber(s));
    }
    if (op == "cross_entropy") {
        double s = 0;
        for (size_t i = 0; i < n; i++) {
            if (x[i] < 0 || y[i] < 0) return Err("probabilities cannot be negative");
            if (x[i] > 0) s -= x[i] * std::log(std::max(y[i], 1e-15));
        }
        return Ok("cross-entropy (natural log) = " + FormatNumber(s));
    }
    return Err("unknown vector operation '" + op + "'");
}

MathResult MatrixOp(const std::string& operation, const std::string& a, const std::string& b) {
    std::string op = Lower(Trim(operation));
    std::vector<std::vector<double>> A, B;
    std::string e;
    if (!ParseMatrix(a, A, e)) return Err("matrix a: " + e);
    int ra = (int)A.size(), ca = (int)A[0].size();
    if (ca > kMaxDim) return Err("matrix too large (max 5x5)");
    if (op == "transpose") {
        std::vector<std::vector<double>> t(ca, std::vector<double>(ra));
        for (int i = 0; i < ra; i++)
            for (int j = 0; j < ca; j++) t[j][i] = A[i][j];
        return Ok("transpose = " + FormatMatrix(t));
    }
    if (op == "determinant" || op == "inverse") {
        if (ra != ca) return Err(op + " needs a square matrix");
        if (op == "determinant") return Ok("determinant = " + FormatNumber(GaussJordan(A, ra, 0, nullptr)));
        std::vector<std::vector<double>> aug = A;
        for (int i = 0; i < ra; i++) {
            aug[i].resize(2 * ra, 0.0);
            aug[i][ra + i] = 1.0;
        }
        std::vector<std::vector<double>> inv;
        double det = GaussJordan(aug, ra, ra, &inv);
        if (std::fabs(det) < 1e-12) return Err("matrix is singular (determinant 0), no inverse");
        return Ok("inverse = " + FormatMatrix(inv));
    }
    if (op == "multiply" || op == "add" || op == "subtract") {
        if (!ParseMatrix(b, B, e)) return Err("matrix b: " + e);
        int rb = (int)B.size(), cb = (int)B[0].size();
        if (cb > kMaxDim) return Err("matrix too large (max 5x5)");
        if (op == "multiply") {
            if (ca != rb) return Err("columns of a must equal rows of b");
            std::vector<std::vector<double>> c(ra, std::vector<double>(cb, 0.0));
            for (int i = 0; i < ra; i++)
                for (int j = 0; j < cb; j++)
                    for (int k = 0; k < ca; k++) c[i][j] += A[i][k] * B[k][j];
            return Ok("a x b = " + FormatMatrix(c));
        }
        if (ra != rb || ca != cb) return Err("matrices must be the same size");
        std::vector<std::vector<double>> c = A;
        for (int i = 0; i < ra; i++)
            for (int j = 0; j < ca; j++) c[i][j] += (op == "add" ? 1 : -1) * B[i][j];
        return Ok((op == "add" ? "a + b = " : "a - b = ") + FormatMatrix(c));
    }
    return Err("unknown matrix operation '" + op + "'");
}

MathResult ClassificationMetrics(int tp, int fp, int fn, int tn) {
    if (tp < 0 || fp < 0 || fn < 0 || tn < 0) return Err("counts cannot be negative");
    double total = tp + fp + fn + tn;
    if (total == 0) return Err("all counts are zero");
    auto ratio = [](double a, double b) { return b == 0 ? std::string("undefined") : FormatNumber(a / b); };
    double precision_d = tp + fp;
    double recall_d = tp + fn;
    std::string f1 = "undefined";
    if (precision_d > 0 && recall_d > 0 && tp > 0) {
        double p = tp / precision_d, r = tp / recall_d;
        f1 = FormatNumber(2 * p * r / (p + r));
    } else if (precision_d > 0 && recall_d > 0) {
        f1 = "0";
    }
    std::string out;
    out.reserve(200);
    out += "accuracy ";
    out += ratio(tp + tn, total);
    out += ", precision ";
    out += ratio(tp, precision_d);
    out += ", recall ";
    out += ratio(tp, recall_d);
    out += ", F1 ";
    out += f1;
    out += ", specificity ";
    out += ratio(tn, tn + fp);
    out += ", false positive rate ";
    out += ratio(fp, fp + tn);
    return Ok(out);
}

}  // namespace deskbuddy_math
