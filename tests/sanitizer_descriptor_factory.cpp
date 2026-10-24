#include "sanitizer_descriptor_control.hpp"
namespace {
struct Implementation : DescriptorControlObject {
    int value() const override { return 42; }
};
}
extern "C" DescriptorControlObject *create_descriptor_control() { return new Implementation; }
extern "C" void destroy_descriptor_control(DescriptorControlObject *p) { delete p; }
