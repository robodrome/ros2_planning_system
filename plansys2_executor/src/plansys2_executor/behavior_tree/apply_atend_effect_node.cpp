// Copyright 2020 Intelligent Robotics Lab
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>
#include <map>
#include <memory>

#include "plansys2_executor/behavior_tree/apply_atend_effect_node.hpp"

namespace plansys2
{

ApplyAtEndEffect::ApplyAtEndEffect(
  const std::string & xml_tag_name,
  const BT::NodeConfiguration & conf)
: ActionNodeBase(xml_tag_name, conf)
{
  action_map_ =
    config().blackboard->get<std::shared_ptr<std::map<std::string, ActionExecutionInfo>>>(
    "action_map");

  action_graph_ = config().blackboard->get<Graph::Ptr>("action_graph");

  bt_builder_ = config().blackboard->get<std::shared_ptr<plansys2::BTBuilder>>("bt_builder");

  problem_client_ =
    config().blackboard->get<std::shared_ptr<plansys2::ProblemExpertClient>>(
    "problem_client");

  node_ = config().blackboard->get<rclcpp_lifecycle::LifecycleNode::SharedPtr>("node");
}

BT::NodeStatus
ApplyAtEndEffect::tick()
{
  std::string action;
  getInput("action", action);

  auto effect = (*action_map_)[action].durative_action_info->at_end_effects;

  if (!(*action_map_)[action].at_end_effects_applied) {
    (*action_map_)[action].at_end_effects_applied = true;
    auto current_time = node_->now();
    auto start_time = (*action_map_)[action].action_executor->get_start_time();
    auto time_from_start = current_time.seconds() - start_time.seconds();
    (*action_map_)[action].at_end_effects_applied_time = time_from_start;
    apply(effect, problem_client_, 0);

    // Update the child input links.
    Node::Ptr child = get_node(action, ActionType::END);
    std::set<std::tuple<Node::Ptr, double, double>> input_arcs;

    for (auto & arc : child->input_arcs) {
      auto parent = std::get<0>(arc);
      auto parent_id = BTBuilder::to_action_id(parent->action, 3);

      double applied_time = 0.0;
      if (parent->action.type == ActionType::START) {
        applied_time = (*action_map_)[parent_id].at_start_effects_applied_time;
      } else if (parent->action.type == ActionType::END) {
        applied_time = (*action_map_)[parent_id].at_end_effects_applied_time;
      }

      double actual_time = current_time.seconds() - applied_time;
      input_arcs.insert(std::make_tuple(parent, actual_time, actual_time));

      // Update the parent output link.
      auto it = std::find_if(
        parent->output_arcs.begin(), parent->output_arcs.end(),
        [&](std::tuple<Node::Ptr, double, double> arc) {
          return std::get<0>(arc) == child;
        });

      parent->output_arcs.erase(*it);
      parent->output_arcs.insert(std::make_tuple(child, actual_time, actual_time));
    }

    child->input_arcs.clear();
    child->input_arcs = input_arcs;
  }

  // Propagate the time bounds.
  bt_builder_->propagate(action_graph_);

  return BT::NodeStatus::SUCCESS;
}

Node::Ptr
ApplyAtEndEffect::get_node(const std::string & node_id, ActionType node_type)
{
  auto it = std::find_if(
    action_graph_->nodes.begin(), action_graph_->nodes.end(),
    [&](Node::Ptr n) {
      auto n_id = BTBuilder::to_action_id(n->action, 3);
      return n_id == node_id && n->action.type == node_type;
    });
  return *it;
}

}  // namespace plansys2
