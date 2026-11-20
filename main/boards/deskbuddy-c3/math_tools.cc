#include "math_tools.h"

#include "math_engine.h"
#include "mcp_server.h"

#include <esp_log.h>

#include <cstdlib>
#include <string>

#define TAG "DeskBuddyMath"

using namespace deskbuddy_math;

namespace {

// Errors are returned as normal text ("Error: ...") rather than as tool failures,
// so the AI can read the reason out and fix the input.
ReturnValue Reply(const MathResult& r) {
    ESP_LOGI(TAG, "%s", r.text.c_str());
    return r.text;
}

// Numbers arrive as text so decimals and expressions like "1/3" or "-2.5e3" work.
bool ToNumber(const std::string& text, double& out, std::string& error) {
    return Evaluate(text, out, error);
}

}  // namespace

void RegisterDeskBuddyMathTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.math.calculate",
        "Exact calculator running on the device. ALWAYS use this for any arithmetic or numeric "
        "answer instead of calculating yourself, then say the result. Supports + - * / % ^ ! "
        "and parentheses, constants pi e, and functions sqrt cbrt abs exp ln log log2 "
        "logb(x,base) pow sin cos tan asin acos atan atan2 (radians), sind cosd tand asind "
        "acosd atand (degrees), sinh cosh tanh floor ceil round(x[,digits]) min max hypot "
        "ncr npr gcd lcm sigmoid relu leaky_relu softplus deg rad. "
        "Example: \"1250*1.08^5\" or \"ncr(52,5)\" or \"sind(30)+log(1000)\". "
        "For percentages write them out, e.g. 18% of 2450 is \"0.18*2450\".",
        PropertyList({Property("expression", kPropertyTypeString)}),
        [](const PropertyList& p) -> ToolResult {
            return Reply(Calculate(p["expression"].value<std::string>()));
        });

    mcp.AddTool(
        "self.math.statistics",
        "Descriptive statistics of a list of numbers: count, sum, mean, median, mode, min, "
        "max, range, quartiles, population and sample variance and standard deviation. "
        "Pass the numbers separated by commas, e.g. \"4, 8, 15, 16, 23, 42\".",
        PropertyList({Property("numbers", kPropertyTypeString)}),
        [](const PropertyList& p) -> ToolResult {
            return Reply(Statistics(p["numbers"].value<std::string>()));
        });

    mcp.AddTool(
        "self.math.solve_quadratic",
        "Solves a*x^2 + b*x + c = 0 and gives real or complex roots and the discriminant. "
        "Pass a, b and c as numbers in text, e.g. a=\"1\", b=\"-3\", c=\"2\".",
        PropertyList({Property("a", kPropertyTypeString), Property("b", kPropertyTypeString),
                      Property("c", kPropertyTypeString)}),
        [](const PropertyList& p) -> ToolResult {
            double a, b, c;
            std::string e;
            if (!ToNumber(p["a"].value<std::string>(), a, e) ||
                !ToNumber(p["b"].value<std::string>(), b, e) ||
                !ToNumber(p["c"].value<std::string>(), c, e)) {
                return Reply({false, "Error: " + e});
            }
            return Reply(SolveQuadratic(a, b, c));
        });

    mcp.AddTool(
        "self.math.solve_linear_system",
        "Solves simultaneous linear equations with up to 5 unknowns (x, y, z, w, v). One "
        "equation per row separated by ';', coefficients then the right-hand side separated "
        "by ','. Example: 2x+3y=13 and x-y=-1 is \"2,3,13; 1,-1,-1\".",
        PropertyList({Property("rows", kPropertyTypeString)}),
        [](const PropertyList& p) -> ToolResult {
            return Reply(SolveLinearSystem(p["rows"].value<std::string>()));
        });

    mcp.AddTool(
        "self.math.vector",
        "Vector maths for machine learning. operation is one of: dot, cosine_similarity, "
        "euclidean_distance, manhattan_distance, norm, softmax, normalize (min-max), "
        "standardize (z-score), mse, mae, rmse, cross_entropy. Vectors are comma separated "
        "numbers. b is needed for dot, distances, cosine_similarity, mse, mae, rmse and "
        "cross_entropy (for mse/mae/rmse a = true values, b = predictions; for cross_entropy "
        "a = true probabilities or one-hot, b = predicted probabilities).",
        PropertyList({Property("operation", kPropertyTypeString), Property("a", kPropertyTypeString),
                      Property("b", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ToolResult {
            return Reply(VectorOp(p["operation"].value<std::string>(), p["a"].value<std::string>(),
                                  p["b"].value<std::string>()));
        });

    mcp.AddTool(
        "self.math.matrix",
        "Matrix maths up to 5x5. operation is one of: determinant, inverse, transpose, "
        "multiply (a times b), add, subtract. Rows separated by ';' and values by ','. "
        "Example: a=\"1,2;3,4\". b is only needed for multiply, add and subtract.",
        PropertyList({Property("operation", kPropertyTypeString), Property("a", kPropertyTypeString),
                      Property("b", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ToolResult {
            return Reply(MatrixOp(p["operation"].value<std::string>(), p["a"].value<std::string>(),
                                  p["b"].value<std::string>()));
        });

    mcp.AddTool(
        "self.math.classification_metrics",
        "Machine learning classification metrics from a confusion matrix: accuracy, precision, "
        "recall, F1, specificity and false positive rate. tp = true positives, fp = false "
        "positives, fn = false negatives, tn = true negatives.",
        PropertyList({Property("tp", kPropertyTypeInteger, 0, 100000000),
                      Property("fp", kPropertyTypeInteger, 0, 100000000),
                      Property("fn", kPropertyTypeInteger, 0, 100000000),
                      Property("tn", kPropertyTypeInteger, 0, 100000000)}),
        [](const PropertyList& p) -> ToolResult {
            return Reply(ClassificationMetrics(p["tp"].value<int>(), p["fp"].value<int>(),
                                               p["fn"].value<int>(), p["tn"].value<int>()));
        });

    ESP_LOGI(TAG, "Math tools registered");
}
