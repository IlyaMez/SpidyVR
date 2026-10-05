#pragma once
namespace spidy {
// A held chord fires once. Both buttons must be released after focus loss or
// startup, so a runtime reconnect cannot unexpectedly switch presentation.
class VrShortcut {
  public:
    bool update(bool focused, bool left, bool right) {
        if (!focused) {
            armed_ = false;
            return false;
        }
        if (!left && !right)
            armed_ = true;
        if (armed_ && left && right) {
            armed_ = false;
            return true;
        }
        return false;
    }

  private:
    bool armed_{};
};
} // namespace spidy
