#include "game.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected assets directory");
        auto physical = one::World::load(std::filesystem::path(argv[1]) / "sandbox.one");
        one::Physics physics;
        physics.rebuild(physical);
        one::DoubleJump jump;
        require(jump.update(true,true,0),"Ground jump was not accepted");
        require(!jump.update(true,false,4),"Held jump repeated");
        jump.update(false,false,2);require(jump.update(true,false,2),"Second jump was not accepted");
        jump.update(false,false,2);require(!jump.update(true,false,2),"Third jump was accepted");
        jump.update(false,true,-1);require(jump.update(true,true,0),"Landing did not reset jumps");
        require(std::string(one::robotsAnimation(true,0,0))=="original_0300000e" &&
                std::string(one::robotsAnimation(true,0,1))=="original_8300007b" &&
                std::string(one::robotsAnimation(false,1,1))=="original_8300006f" &&
                std::string(one::robotsAnimation(false,-1,1))=="original_83000064","Rodney clip selection failed");
        
        int sounds = 0;
        auto game = one::createSandboxGame();
        one::GameServices services{physical, physics, [&] { ++sounds; }};
        game->start(services);
        physics.resetPlayer({-3, .8f, 4});
        game->fixedUpdate(services, 1.f / 60);
        require(game->state().collected == 1 && sounds == 1, "Collectible gameplay event failed");
        game->fixedUpdate(services, 1.f / 60);
        require(game->state().collected == 1, "Collected item counted twice");
        game->start(services);
        require(game->state().collected == 1, "Gameplay restart lost saved collection state");
        std::cout << "PASS double jump, Rodney clip policy, collectible events and restart persistence\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
