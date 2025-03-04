/*
 * Copyright (C) 2022 Open Source Robotics Foundation
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

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <mutex>

#include <ignition/msgs.hh>
#include <ignition/transport/Node.hh>
#include <ignition/utilities/ExtraTestMacros.hh>
#include "ignition/gazebo/Model.hh"
#include "ignition/gazebo/Server.hh"
#include "ignition/gazebo/test_config.hh"  // NOLINT(build/include)
#include "../helpers/EnvTestFixture.hh"

using namespace ignition;
using namespace gazebo;

/////////////////////////////////////////////////
class RFCommsTest : public InternalFixture<::testing::Test>
{
};

/////////////////////////////////////////////////
TEST_F(RFCommsTest, IGN_UTILS_TEST_DISABLED_ON_WIN32(RFComms))
{
  // Start server
  ServerConfig serverConfig;
  const auto sdfFile =
    ignition::common::joinPaths(std::string(PROJECT_SOURCE_PATH),
      "examples", "worlds", "rf_comms.sdf");
  serverConfig.SetSdfFile(sdfFile);

  Server server(serverConfig);
  EXPECT_FALSE(server.Running());
  EXPECT_FALSE(*server.Running(0));

  // Run server
  size_t iters = 1000;
  server.Run(true, iters, false);

  unsigned int msgCounter = 0u;
  std::mutex mutex;
  auto cb = [&](const msgs::Dataframe &_msg) -> void
  {
    // Verify msg content
    std::lock_guard<std::mutex> lock(mutex);
    std::string expected = "hello world " + std::to_string(msgCounter);

    ignition::msgs::StringMsg receivedMsg;
    receivedMsg.ParseFromString(_msg.data());
    EXPECT_EQ(expected, receivedMsg.data());
    msgCounter++;
  };

  // Create subscriber.
  ignition::transport::Node node;
  std::string addr  = "addr1";
  std::string subscriptionTopic = "addr1/rx";

  // Subscribe to a topic by registering a callback.
  auto cbFunc = std::function<void(const msgs::Dataframe &)>(cb);
  EXPECT_TRUE(node.Subscribe(subscriptionTopic, cbFunc))
      << "Error subscribing to topic [" << subscriptionTopic << "]";

  // Create publisher.
  std::string publicationTopic = "/broker/msgs";
  auto pub = node.Advertise<ignition::msgs::Dataframe>(publicationTopic);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Prepare the message.
  ignition::msgs::Dataframe msg;
  msg.set_src_address("addr2");
  msg.set_dst_address(addr);

  // Publish 10 messages.
  ignition::msgs::StringMsg payload;
  unsigned int pubCount = 10u;
  for (unsigned int i = 0u; i < pubCount; ++i)
  {
    // Prepare the payload.
    payload.set_data("hello world " + std::to_string(i));
    std::string serializedData;
    EXPECT_TRUE(payload.SerializeToString(&serializedData))
        << payload.DebugString();
    msg.set_data(serializedData);
    EXPECT_TRUE(pub.Publish(msg));
    server.Run(true, 100, false);
  }

  // Verify subscriber received all msgs.
  int sleep = 0;
  bool done = false;
  while (!done && sleep++ < 10)
  {
    std::lock_guard<std::mutex> lock(mutex);
    done = msgCounter == pubCount;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  EXPECT_EQ(pubCount, msgCounter);
}
