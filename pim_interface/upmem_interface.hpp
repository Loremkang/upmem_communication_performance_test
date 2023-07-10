#include "pim_interface.hpp"

class UPMEMInterface : PIMInterface {
    void SendToPIM(uint8_t** buffer, string symbol_name, uint8_t length) {
    }

    void ReceiveFromPIM(uint8_t** buffer, uint64_t pim_offset, uint8_t length) {

    }
}