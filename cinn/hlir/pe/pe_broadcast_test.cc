#include <gtest/gtest.h>

#include "cinn/backends/llvm/execution_engine.h"
#include "cinn/cinn.h"
#include "cinn/common/target.h"
#include "cinn/common/test_helper.h"
#include "cinn/hlir/pe/broadcast.h"
#include "cinn/runtime/cpu/host_intrinsics.h"

namespace cinn {
namespace hlir {
namespace pe {
using ir::Tensor;

void TestBroadcastPE(
    const std::string &fn_name,
    Tensor (*func_op)(const Tensor &A, const Tensor &B, const std::string &output_name, const Expr &axis),
    float (*fn_runtime)(float, float),
    int set_value = 0) {
  Expr M(100), N(32);

  Placeholder<float> A("A", {M, N});
  Placeholder<float> B("B", {M, N});

  auto C = func_op(A.tensor(), B.tensor(), "C", Expr());

  auto stages = CreateStages({C});

  Target target = common::DefaultHostTarget();
  Module::Builder builder("module0", target);
  auto func = Lower("fn", stages, {A, B, C});
  builder.AddFunction(func);
  LOG(INFO) << "func:\n" << func;

  auto jit    = backends::ExecutionEngine::Create({});
  auto module = builder.Build();

  jit->Link(module);
  auto fn = jit->Lookup("fn");
  CHECK(fn);
  auto fn_ = reinterpret_cast<void (*)(void *, int32_t)>(fn);

  cinn_buffer_t *A_buf;
  cinn_buffer_t *B_buf;
  if (set_value != 0) {
    A_buf = common::BufferBuilder(Float(32), {100, 32}).set_val(set_value).Build();
    B_buf = common::BufferBuilder(Float(32), {100, 32}).set_val(set_value).Build();
  } else {
    A_buf = common::BufferBuilder(Float(32), {100, 32}).set_random().Build();
    B_buf = common::BufferBuilder(Float(32), {100, 32}).set_random().Build();
  }
  auto *C_buf = common::BufferBuilder(Float(32), {100, 32}).set_zero().Build();

  cinn_pod_value_t a_arg(A_buf), b_arg(B_buf), c_arg(C_buf);
  cinn_pod_value_t args[] = {a_arg, b_arg, c_arg};
  fn_(args, 3);

  auto *ad = reinterpret_cast<float *>(A_buf->memory);
  auto *bd = reinterpret_cast<float *>(B_buf->memory);
  auto *cd = reinterpret_cast<float *>(C_buf->memory);
  for (int i = 0; i < A_buf->num_elements(); i++) {
    ASSERT_NEAR(cd[i], fn_runtime(ad[i], bd[i]), 1e-5);
  }
}

#define RULE(test_name__, rule__) \
  float test_name__(float a, float b) { rule__ }

#define TEST_BROADCAST_PE_FP32_BASIC(test_name__) \
  TEST(elementwise_pe, test_name__) { TestBroadcastPE("PE_Broadcast_" #test_name__ "_fp32", test_name__, test_name__); }
#define TEST_BROADCAST_PE_FP32_SET_BASIC(test_name__) \
  TEST(elementwise_pe, test_name__) { TestBroadcastPE("PE_Broadcast_" #test_name__ "_fp32", test_name__, value); }

#define TEST_BROADCAST_PE_FP32(test_name__, rule__) \
  RULE(test_name__, rule__)                         \
  TEST_BROADCAST_PE_FP32_BASIC(test_name__)

TEST_BROADCAST_PE_FP32(Add, return a + b;)

}  // namespace pe
}  // namespace hlir
}  // namespace cinn