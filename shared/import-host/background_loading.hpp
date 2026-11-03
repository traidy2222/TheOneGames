#pragma once
#include <theone/physics/scene_loading.hpp>
#include <theone/physics/edit.hpp>
#include <theone/assets/load_job.hpp>
#include <functional>
struct BackgroundLoads {
    one::PhysicsSceneLoadJob scene;
    one::PhysicsEditJob collision;
    one::ModelLoadJob model;
    one::ImageLoadJob image;
    bool importing = false;
    std::weak_ptr<one::Model> imageOwner;
    size_t imageIndex = 0;
    void cancel();
    void retireHistory(std::vector<one::World>& undo, std::vector<one::World>& redo);
    bool busy() const;
    void draw();
    void poll(const std::function<void(std::shared_ptr<one::PhysicsScene>)>& applyScene,
              const std::function<void(std::shared_ptr<one::Model>, bool)>& applyModel,
              const std::function<void(std::shared_ptr<one::PhysicsEdit>)>& applyCollision,
              const std::function<bool(const std::shared_ptr<one::Model>&)>& liveModel,
              const std::function<void(std::string)>& error);
};

