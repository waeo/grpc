/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/transport/bdp_estimator.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include <gtest/gtest.h>
#include <limits.h>
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

extern "C" gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

namespace grpc_core {
namespace testing {
namespace {
int g_clock = 0;

gpr_timespec fake_gpr_now(gpr_clock_type clock_type) {
  return (gpr_timespec){
      .tv_sec = g_clock, .tv_nsec = 0, .clock_type = clock_type,
  };
}

void inc_time(void) { g_clock += 30; }
}  // namespace

TEST(BdpEstimatorTest, NoOp) { BdpEstimator est("test"); }

TEST(BdpEstimatorTest, EstimateBdpNoSamples) {
  BdpEstimator est("test");
  int64_t estimate;
  est.EstimateBdp(&estimate);
}

namespace {
void AddSamples(BdpEstimator *estimator, int64_t *samples, size_t n) {
  estimator->AddIncomingBytes(1234567);
  inc_time();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  EXPECT_TRUE(estimator->NeedPing(&exec_ctx));
  estimator->SchedulePing();
  estimator->StartPing();
  for (size_t i = 0; i < n; i++) {
    estimator->AddIncomingBytes(samples[i]);
    EXPECT_FALSE(estimator->NeedPing(&exec_ctx));
  }
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(1, GPR_TIMESPAN)));
  grpc_exec_ctx_invalidate_now(&exec_ctx);
  estimator->CompletePing(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
}

void AddSample(BdpEstimator *estimator, int64_t sample) {
  AddSamples(estimator, &sample, 1);
}
}  // namespace

TEST(BdpEstimatorTest, GetEstimate1Sample) {
  BdpEstimator est("test");
  AddSample(&est, 100);
  int64_t estimate;
  est.EstimateBdp(&estimate);
}

TEST(BdpEstimatorTest, GetEstimate2Samples) {
  BdpEstimator est("test");
  AddSample(&est, 100);
  AddSample(&est, 100);
  int64_t estimate;
  est.EstimateBdp(&estimate);
}

TEST(BdpEstimatorTest, GetEstimate3Samples) {
  BdpEstimator est("test");
  AddSample(&est, 100);
  AddSample(&est, 100);
  AddSample(&est, 100);
  int64_t estimate;
  est.EstimateBdp(&estimate);
}

namespace {
static int64_t GetEstimate(const BdpEstimator &estimator) {
  int64_t out;
  EXPECT_TRUE(estimator.EstimateBdp(&out));
  return out;
}

int64_t NextPow2(int64_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;
  v++;
  return v;
}
}  // namespace

class BdpEstimatorRandomTest : public ::testing::TestWithParam<size_t> {};

TEST_P(BdpEstimatorRandomTest, GetEstimateRandomValues) {
  BdpEstimator est("test");
  const int kMaxSample = 65535;
  int min = kMaxSample;
  int max = 0;
  for (size_t i = 0; i < GetParam(); i++) {
    int sample = rand() % (kMaxSample + 1);
    if (sample < min) min = sample;
    if (sample > max) max = sample;
    AddSample(&est, sample);
    if (i >= 3) {
      EXPECT_LE(GetEstimate(est), GPR_MAX(65536, 2 * NextPow2(max)))
          << " min:" << min << " max:" << max << " sample:" << sample;
    }
  }
}

INSTANTIATE_TEST_CASE_P(TooManyNames, BdpEstimatorRandomTest,
                        ::testing::Values(3, 4, 6, 9, 13, 19, 28, 42, 63, 94,
                                          141, 211, 316, 474, 711));
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  gpr_now_impl = grpc_core::testing::fake_gpr_now;
  grpc_init();
  grpc_timer_manager_set_threading(false);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
