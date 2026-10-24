#pragma once
// A plain external provider keeps the driver's first vptr check cold until EMFILE.
struct DescriptorControlObject {
    virtual ~DescriptorControlObject() = default;
    virtual int value() const = 0;
};
extern "C" DescriptorControlObject *create_descriptor_control();
extern "C" void destroy_descriptor_control(DescriptorControlObject *);
