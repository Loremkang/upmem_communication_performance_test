#include "direct_interface.hpp"

#define cast_to_cpp(name) reinterpret_cast<DirectPIMInterface*>(name)
extern "C" {
  typedef XDirectPIMInterface XDPI
  #include <upmem_direct_c.h>
  XDirectPIMInterface* newDirectPIMInterface() {
    return reinterpret_cast<XDirectPIMInterface*>(new DirectPIMInterface());
  }
  void deleteDirectPIMInterface(XDirectPIMInterface self) {
    delete (cast_to_cpp(self))
  }
  void SendToRank(XDirectPIMInterface* self, uint8_t **buffers, uint32_t symbol_offset, uint8_t *ptr_dest, uint32_t length, int id) {
    cast_to_cpp(self)->SendToRank(buffers, symbol_offset, ptr_dest, length, id);
  }
  void allocate(XDirectPIMInterface* self, uint32_t nr_of_ranks, std::string binary) {
    cast_to_cpp(self)->allocate(nr_of_ranks, binary);
  }
  void deallocate(XDPI self){
    cast_to_cpp(self)->deallocate();
  }
  void Launch(XDPI self, bool async) {
    cast_to_cpp(self)->Launch(async;)
  }
  void sync(XDPI self) {
    cast_to_cpp(self)->sync();
  }
  void PrintLog(XDPI self) {
    cast_to_cpp(self)->PrintLog(); 
  }
  void SendToPIM(XDPI self, uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async) {
    cast_to_cpp(self)->SendToPIM(buffers, symbol_name, symbol_offset, length, sync)
  }
  
  void ReceiveFromPIM(XDPI self, uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async) {
    cast_to_cpp(self)->ReceiveFromPIM(buffers, symbol_name, symbol_offset, length, async)
  }

}
