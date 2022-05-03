/*
 * Copyright (C) 2021 Open Source Robotics Foundation
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

#include "ForceTorque.hh"
#include "SensorImpl.hh"

#include <unordered_map>
#include <utility>
#include <string>

#include <ignition/plugin/Register.hh>

#include <sdf/Element.hh>

#include <ignition/common/Profiler.hh>

#include <ignition/sensors/ForceTorqueSensor.hh>

#include "ignition/gazebo/components/ChildLinkName.hh"
#include "ignition/gazebo/components/ForceTorque.hh"
#include "ignition/gazebo/components/Joint.hh"
#include "ignition/gazebo/components/JointTransmittedWrench.hh"
#include "ignition/gazebo/components/Link.hh"
#include "ignition/gazebo/components/Name.hh"
#include "ignition/gazebo/components/ParentEntity.hh"
#include "ignition/gazebo/components/ParentLinkName.hh"
#include "ignition/gazebo/components/Pose.hh"
#include "ignition/gazebo/components/Sensor.hh"
#include "ignition/gazebo/components/World.hh"
#include "ignition/gazebo/EntityComponentManager.hh"
#include "ignition/gazebo/Util.hh"

using namespace ignition;
using namespace gazebo;
using namespace systems;

/// \brief Private ForceTorque data class.
class ignition::gazebo::systems::ForceTorquePrivate
: public SensorImpl<sensors::ForceTorqueSensor, components::ForceTorque>
{
  /// \brief A struct to hold the joint and link entities associated with a
  /// sensor
  public: struct SensorJointAndLinks
  {
    /// \brief The parent joint of the sensor
    Entity joint;
    /// \breif The parent link of the joint
    Entity jointParentLink;
    /// \breif The child link of the joint
    Entity jointChildLink;
  };

  /// \brief Cache of the entities associated with the sensor
  public: std::unordered_map<Entity, SensorJointAndLinks> sensorJointLinkMap;

  /// \brief Get the link entity identified by the given scoped name
  /// \param[in] _ecm Immutable reference to ECM.
  /// \param[in] _name Scoped name of the link
  /// \param[in] _parentModel The model entity in which the scope of the given
  /// name starts.
  /// \return The link entity if found, otherwise kNullEntity
  public: Entity GetLinkFromScopedName(const EntityComponentManager &_ecm,
                                       const std::string &_name,
                                       Entity _parentModel) const;

  public: void AddSensor(
    const EntityComponentManager &_ecm,
    const Entity _entity,
    const components::ForceTorque *_sensor,
    const components::ParentEntity *_parent) override;

  public: void CreateComponents(
    EntityComponentManager &_ecm,
    const Entity _entity,
    const sensors::ForceTorqueSensor *_sensor) override;

  public: void Update(const EntityComponentManager &_ecm) override;
};

//////////////////////////////////////////////////
ForceTorque::ForceTorque()
    : dataPtr(std::make_unique<ForceTorquePrivate>())
{
}

//////////////////////////////////////////////////
ForceTorque::~ForceTorque() = default;

//////////////////////////////////////////////////
void ForceTorque::PreUpdate(const UpdateInfo &_info,
    EntityComponentManager &_ecm)
{
  this->dataPtr->PreUpdate(_info, _ecm);
}

//////////////////////////////////////////////////
void ForceTorque::PostUpdate(const UpdateInfo &_info,
                     const EntityComponentManager &_ecm)
{
  this->dataPtr->PostUpdate(_info, _ecm);
}

//////////////////////////////////////////////////
void ForceTorquePrivate::CreateComponents(
    EntityComponentManager &_ecm,
    const Entity _entity,
    const sensors::ForceTorqueSensor *_sensor)
{
  // Set topic
  _ecm.CreateComponent(_entity, components::SensorTopic(_sensor->Topic()));

  // Enable wrench measurements on the joint
  auto jointEntity =
    _ecm.Component<components::ParentEntity>(_entity)->Data();
  _ecm.CreateComponent(jointEntity, components::JointTransmittedWrench());
}

//////////////////////////////////////////////////
void ForceTorquePrivate::AddSensor(
  const EntityComponentManager &_ecm,
  const Entity _entity,
  const components::ForceTorque *_ft,
  const components::ParentEntity *_parent)
{
  // create sensor
  std::string sensorScopedName =
      removeParentScope(scopedName(_entity, _ecm, "::", false), "::");
  sdf::Sensor data = _ft->Data();
  data.SetName(sensorScopedName);
  // check topic
  if (data.Topic().empty())
  {
    std::string topic = scopedName(_entity, _ecm) + "/forcetorque";
    data.SetTopic(topic);
  }
  std::unique_ptr<sensors::ForceTorqueSensor> sensor =
      this->sensorFactory.CreateSensor<
      sensors::ForceTorqueSensor>(data);
  if (nullptr == sensor)
  {
    ignerr << "Failed to create sensor [" << sensorScopedName << "]"
           << std::endl;
    return;
  }

  auto jointEntity = _parent->Data();
  const std::string jointName =
      _ecm.Component<components::Name>(jointEntity)->Data();

  // Parent has to be a joint
  if (!_ecm.EntityHasComponentType(jointEntity,
                                   components::Joint::typeId))
  {
    ignerr << "Parent entity of sensor [" << sensorScopedName
           << "] must be a joint. Failed to create sensor." << std::endl;
    return;
  }

  const auto modelEntity =
      _ecm.Component<components::ParentEntity>(jointEntity)->Data();

  // Find the joint parent and child links
  const auto jointParentName =
      _ecm.Component<components::ParentLinkName>(jointEntity)->Data();
  auto jointParentLinkEntity =
      this->GetLinkFromScopedName(_ecm, jointParentName, modelEntity);
  if (kNullEntity == jointParentLinkEntity )
  {
    ignerr << "Parent link with name [" << jointParentName
           << "] of joint with name [" << jointName
           << "] not found. Failed to create sensor [" << sensorScopedName
           << "]" << std::endl;
    return;
  }

  const auto jointChildName =
      _ecm.Component<components::ChildLinkName>(jointEntity)->Data();
  auto jointChildLinkEntity =
      this->GetLinkFromScopedName(_ecm, jointChildName, modelEntity);
  if (kNullEntity == jointChildLinkEntity)
  {
    ignerr << "Child link with name [" << jointChildName
           << "] of joint with name [" << jointName
           << "] not found. Failed to create sensor [" << sensorScopedName
           << "]" << std::endl;
    return;
  }

  SensorJointAndLinks sensorJointLinkEntry;
  sensorJointLinkEntry.joint = jointEntity;
  sensorJointLinkEntry.jointParentLink = jointParentLinkEntity;
  sensorJointLinkEntry.jointChildLink = jointChildLinkEntity;
  this->sensorJointLinkMap[_entity] = sensorJointLinkEntry;

  const auto X_WC = worldPose(jointChildLinkEntity, _ecm);
  const auto X_CJ = _ecm.Component<components::Pose>(jointEntity)->Data();
  const auto X_WJ = X_WC * X_CJ;
  const auto X_JS = _ecm.Component<components::Pose>(_entity)->Data();
  const auto X_WS = X_WJ * X_JS;
  const auto X_SC = X_WS.Inverse() * X_WC;
  sensor->SetRotationChildInSensor(X_SC.Rot());

  this->entitySensorMap.insert(
      std::make_pair(_entity, std::move(sensor)));
  this->newSensors.insert(_entity);
}

//////////////////////////////////////////////////
Entity ForceTorquePrivate::GetLinkFromScopedName(
    const EntityComponentManager &_ecm, const std::string &_name,
    Entity _parentModel) const
{
  auto entities = entitiesFromScopedName(_name, _ecm, _parentModel);
  for (const auto & entity : entities)
  {
    if (_ecm.EntityHasComponentType(entity, components::Link::typeId))
    {
      return entity;
    }
  }
  return kNullEntity;
}

//////////////////////////////////////////////////
void ForceTorquePrivate::Update(const EntityComponentManager &_ecm)
{
  IGN_PROFILE("ForceTorquePrivate::Update");
  _ecm.Each<components::ForceTorque>(
      [&](const Entity &_entity, const components::ForceTorque *) -> bool
      {
        auto it = this->entitySensorMap.find(_entity);
        if (it != this->entitySensorMap.end())
        {
          auto jointLinkIt = this->sensorJointLinkMap.find(_entity);
          if (jointLinkIt == this->sensorJointLinkMap.end())
          {
            ignerr << "Failed to update Force/Torque Sensor: " << _entity
                   << ". Associated entities not found." << std::endl;
            return true;
          }

          auto jointWrench = _ecm.Component<components::JointTransmittedWrench>(
              jointLinkIt->second.joint);

          // Notation:
          // X_WJ: Pose of joint in world
          // X_WP: Pose of parent link in world
          // X_WC: Pose of child link in world
          // X_WS: Pose of sensor in world
          // X_SP: Pose of parent link in sensors frame
          // X_SC: Pose of child link in sensors frame
          const auto X_WP =
              worldPose(jointLinkIt->second.jointParentLink, _ecm);
          const auto X_WC = worldPose(jointLinkIt->second.jointChildLink, _ecm);
          // There appears to be a bug worldPose for computing poses of //joint
          // and its children, so we do it manually here.
          const auto X_CJ =
              _ecm.Component<components::Pose>(jointLinkIt->second.joint)
                  ->Data();
          auto X_WJ = X_WC * X_CJ;

          auto X_JS = _ecm.Component<components::Pose>(_entity)->Data();
          auto X_WS = X_WJ * X_JS;
          auto X_SP = X_WS.Inverse() * X_WP;

          // The joint wrench is computed at the joint frame. We need to
          // transform it the sensor frame.
          math::Vector3d force =
              X_JS.Rot().Inverse() * msgs::Convert(jointWrench->Data().force());

          math::Vector3d torque =
              X_JS.Rot().Inverse() *
                  msgs::Convert(jointWrench->Data().torque()) -
              X_JS.Pos().Cross(force);

          it->second->SetForce(force);
          it->second->SetTorque(torque);
          it->second->SetRotationParentInSensor(X_SP.Rot());
        }
        else
        {
          ignerr << "Failed to update Force/Torque Sensor: " << _entity << ". "
                 << "Entity not found." << std::endl;
        }

        return true;
      });
}

IGNITION_ADD_PLUGIN(ForceTorque, System,
  ForceTorque::ISystemPreUpdate,
  ForceTorque::ISystemPostUpdate
)

IGNITION_ADD_PLUGIN_ALIAS(ForceTorque, "ignition::gazebo::systems::ForceTorque")
