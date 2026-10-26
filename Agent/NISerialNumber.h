#ifndef RK3_NI_SERIAL_NUMBER_H
#define RK3_NI_SERIAL_NUMBER_H
#include <string>

namespace rk3 { namespace ni {
struct DeviceInformation;
// Caller owns one coherent cache/connection snapshot and keeps it stable for
// this call. False guards preserve output; conversion exceptions propagate.
bool get_serial_number(const DeviceInformation* cached_info,
                       bool connected, std::string& output);
}}
#endif
