#include "visible_profile.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <SDL3/SDL.h>
#include <stdexcept>
VisibleProfile::VisibleProfile(const std::filesystem::path& path, int frames, bool probe) : limit_(frames) {
    if (path.empty())
        return;
    if ((!probe && (frames < 1200 || frames > 10000)) || (probe && frames != 240))
        throw std::runtime_error("Visible profile requires 1200-10000 frames");
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    output.open(path);
    if (!output)
        throw std::runtime_error("Cannot open visible trace");
    output << "frame,phase,metric,value\n";
    output << std::setprecision(15);
}
void VisibleProfile::begin(bool ready) {
    if (!enabled())
        return;
    started = previous = Clock::now();
    if (step_ < 0 && ready)
        step_ = 0;
    phase_ = step_ < 0     ? "startup"
             : step_ < 240 ? "gameplay"
             : step_ < 480 ? "editor_idle"
             : step_ < 540 ? "drag"
             : step_ < 600 ? "drag_settle"
             : step_ < 660 ? "undo"
             : step_ < 720 ? "redo"
             : step_ < 900 ? "asset_reload"
                           : "scene_reload";
}
double VisibleProfile::mark() {
    if (!enabled())
        return 0;
    auto now = Clock::now();
    auto ms = std::chrono::duration<double, std::milli>(now - previous).count();
    previous = now;
    return ms;
}
void VisibleProfile::sample(const std::string& name, double ms) {
    if (!enabled())
        return;
    output << step_ << ',' << phase_ << ',' << name << ',' << ms << '\n';
    samples[phase_ + "." + name].push_back(ms);
}
void VisibleProfile::end(const one::FrameStats& s, size_t textureBytes, bool busy, bool visible) {
    if (!enabled())
        return;
    sample("width", s.width); sample("height", s.height); sample("present_mode", s.presentMode);
    sample("swapchain_images", s.swapchainImages); sample("swapchain_generation", double(s.swapchainGeneration));
    sample("swapchain_rebuild", s.swapchainRebuildMs); sample("swapchain_idle", s.swapchainIdleMs);
    sample("swapchain_targets", s.swapchainTargetsMs);
    for (size_t i = 0; i < s.cpuStages.size(); ++i) {
        sample(std::string("cpu_wall_") + one::cpuStageNames[i], s.cpuStages[i].wallMs);
        sample(std::string("cpu_cycles_") + one::cpuStageNames[i], double(s.cpuStages[i].cycles));
    }
    sample("renderer_thread_ms", s.cpuStages[0].threadMs);
    sample("material_created", double(s.materialCreations)); sample("material_deferred", double(s.materialDeferred));
    sample("material_warmups", double(s.materialWarmups)); sample("material_work", s.materialWorkMs);
    sample("alias_lookups", double(s.textureAliasLookups)); sample("alias_repairs", double(s.textureAliasRepairs));
    if (s.submitHostMs && inputHostMs && pollHostMs) {
        sample("sampled_input_to_submit", s.submitHostMs - inputHostMs);
        sample("polled_input_to_submit", s.submitHostMs - pollHostMs);
    }
    sample("renderer_cpu", s.prepareMs + s.recordMs + s.memoryStatsMs);
    sample("invalidation", s.invalidationMs); sample("invalidation_caches", s.invalidationCachesMs);
    sample("invalidation_materials", s.invalidationMaterialsMs);
    sample("invalidation_textures", s.invalidationTexturesMs);
    sample("invalidation_geometry", s.invalidationGeometryMs);
    sample("invalidation_calls", double(s.invalidationCalls));
    sample("invalidated_bounds", double(s.invalidatedBounds));
    sample("invalidated_shadow_templates", double(s.invalidatedShadowTemplates));
    sample("shadow_plans_reused", double(s.shadowPlansReused));
    sample("shadow_keys_compared", double(s.shadowKeysCompared));
    sample("shadow_keys_recorded", double(s.shadowKeysRecorded));
    sample("shadow_plans_rebuilt", double(s.shadowPlansRebuilt));
    sample("shadow_plan_check", s.shadowPlanCheckMs);
    sample("shadow_plan_rebuild", s.shadowPlanRebuildMs);
    sample("shadow_plan_reference", s.shadowPlanReference ? 1 : 0);
    sample("shadow_binding_reference", s.shadowBindingReference ? 1 : 0);
    sample("shadow_template_rebinds", double(s.shadowTemplateRebinds));
    sample("shadow_lod_selections", double(s.shadowLodSelections));
    sample("invalidation_primitives_visited", double(s.invalidationPrimitivesVisited));
    sample("invalidation_cache_entries_scanned", double(s.invalidationCacheEntriesScanned));
    sample("draw_scratch_growths", double(s.drawScratchGrowths));
    sample("frame_id",double(s.frame));sample("waited_frame",double(s.waitedFrame));
    sample("gpu_frame",double(s.gpuFrame));sample("gpu_total",s.gpuTotalMs);sample("gpu_upload",s.gpuUploadMs);
    sample("gpu_main",s.gpuMainMs);sample("gpu_shadow",s.gpuShadowMs);
    sample("fence_pending",s.fencePending?1:0);sample("fence_thread_cpu",s.fenceThreadCpuMs);
    sample("submit_age",s.submitAgeMs);sample("submit",s.submitMs);
    sample("calibrated", s.calibratedTimingAvailable ? 1 : 0);
    sample("gpu_start_host", s.gpuStartHostMs); sample("gpu_end_host", s.gpuEndHostMs);
    sample("submit_to_gpu", s.submitToGpuMs); sample("gpu_done_to_fence", s.gpuDoneToFenceMs);
    sample("calibration_deviation", s.calibrationDeviationMs);
    sample("submit_host", s.submitHostMs); sample("present_host", s.presentHostMs);
    sample("presentation_wait", s.presentationWaitMs);
    sample("presentation_wait_id", double(s.presentationWaitId));
    sample("presentation_wait_polls", double(s.presentationWaitPolls));
    sample("presentation_check", s.presentationCheckMs);
    sample("presentation_sleep", s.presentationSleepMs);
    sample("presentation_worker_id", double(s.presentationWorkerId));
    sample("presentation_worker_wait", s.presentationWorkerMs);
    sample("presentation_wait_budget", s.presentationWaitBudgetMs);
    sample("presentation_wait_host", s.presentationWaitHostMs);
    sample("presentation_wait_thread_cpu", s.presentationWaitThreadCpuMs);
    sample("presentation_pacing", s.presentationPacing ? 1 : 0);
    sample("presentation_timeout", s.presentationWaitTimedOut ? 1 : 0);
    sample("staging_reuses",double(s.stagingReuses));sample("staging_allocations",double(s.stagingAllocations));
    sample("draw_order_reuses",double(s.drawOrderReuses));sample("shadow_command_reuses",double(s.shadowCommandReuses));
    sample("shadow_command_updates",double(s.shadowCommandUpdates));
    sample("upload_commands",double(s.uploadCommands));sample("transfer_bytes",double(s.transferBytes));
    sample("main_draws",double(s.mainDraws));sample("shadow_plan",s.shadowPlanMs);

    sample("present", s.presentMs);
    sample("acquire", s.acquireWaitMs);
    sample("fence", s.fenceWaitMs);
    sample("draw_list", s.drawListMs);
    sample("main_record", s.mainRecordMs);
    sample("geometry", s.geometryPrepareMs);
    sample("retirement", s.retirementMs);
    sample("upload_release", s.uploadReleaseMs);
    sample("destroyed", double(s.resourcesDestroyed));
    sample("retired", double(s.retiredResources));
    sample("changes_queued", double(s.textureChangesQueued));
    sample("changes_committed", double(s.textureChangesCommitted));
    sample("promotions", double(s.texturePromotions));
    sample("demotions", double(s.textureDemotions));
    sample("residency", s.residencyMs);
    sample("transition_demand", double(s.textureTransitions));
    sample("upload", s.uploadCpuMs);
    sample("pending_draws", double(s.pendingDraws));
    sample("texture_bytes", double(textureBytes));
    sample("busy", busy ? 1 : 0);
    sample("visible", visible ? 1 : 0);
    sample("frame_wall", std::chrono::duration<double, std::milli>(Clock::now() - started).count());
    if (step_ >= 0)
        ++step_;
}
void VisibleProfile::finish() {
    if (!enabled())
        return;
    output.flush();
    for (auto& [name, values] : samples) {
        std::sort(values.begin(), values.end());
        double sum = 0;
        for (auto v : values)
            sum += v;
        std::cout << "VISIBLE " << name << " mean=" << sum / values.size()
                  << " p95=" << values[size_t((values.size() - 1) * .95)]
                  << " p99=" << values[size_t((values.size() - 1) * .99)] << " max=" << values.back() << '\n';
    }
}

void VisibleProfile::inputSampled() {
    if (enabled()) inputHostMs = double(SDL_GetPerformanceCounter()) * 1000.0 / double(SDL_GetPerformanceFrequency());
}
void VisibleProfile::pollInput() {
    if (enabled()) pollHostMs = double(SDL_GetPerformanceCounter()) * 1000.0 / double(SDL_GetPerformanceFrequency());
}
