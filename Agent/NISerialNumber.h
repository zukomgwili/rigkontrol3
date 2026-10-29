#ifndef RK3_NI_SERIAL_NUMBER_H
#define RK3_NI_SERIAL_NUMBER_H
#include <string>

namespace rk3 { namespace ni {
struct DeviceInformation;
// Caller owns one coherent cache/connection snapshot and keeps it stable for
// this call. False guards preserve output; conversion exceptions propagate.
bool get_serial_number(const DeviceInformation* cached_info,
                       bool connected, std::string& output);
// Input borrowing ends before output assignment, which may update an aliased
// mutable cached field. Counted COPY and conversion exceptions are preserved.
bool get_serial_number(const DeviceInformation* cached_info,
                       bool connected, std::u16string& output);
}}
#endif
