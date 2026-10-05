// Read-only runtime/device check. Never creates or starts an XR session.
#include <algorithm>
#include <array>
#include <iostream>
#include <iterator>
#include <openxr/openxr.h>

int main() {
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    const char name[] = "Spidy headset preflight";
    std::copy(std::begin(name), std::end(name), create.applicationInfo.applicationName);
    create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    XrInstance instance{};
    auto result = xrCreateInstance(&create, &instance);
    if (XR_FAILED(result)) {
        std::cerr << "OpenXR instance unavailable: " << result << '\n';
        return 2;
    }
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system{};
    result = xrGetSystem(instance, &info, &system);
    int status = 0;
    if (XR_SUCCEEDED(result)) {
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        result = xrGetSystemProperties(instance, system, &properties);
        if (XR_SUCCEEDED(result)) {
            std::cout << "Headset available: " << properties.systemName
                      << "; position tracking=" << properties.trackingProperties.positionTracking
                      << "; orientation tracking=" << properties.trackingProperties.orientationTracking
                      << ". No session was started.\n";
            std::array<XrViewConfigurationView, 2> views{
                {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
            uint32_t count{};
            result = xrEnumerateViewConfigurationViews(
                instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views.data());
            if (XR_SUCCEEDED(result) && count == 2)
                for (unsigned i = 0; i < 2; ++i)
                    std::cout << "Recommended eye " << i << ": " << views[i].recommendedImageRectWidth << 'x'
                              << views[i].recommendedImageRectHeight << '\n';
        }
    }
    if (XR_FAILED(result)) {
        char error[XR_MAX_RESULT_STRING_SIZE]{};
        xrResultToString(instance, result, error);
        std::cerr << "Headset unavailable: " << error << " (" << result << ").\n";
        status = 3;
    }
    xrDestroyInstance(instance);
    return status;
}
