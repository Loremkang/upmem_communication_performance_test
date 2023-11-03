typedef struct XDirectPIMInterface XDirectPimInterface;

void deleteDirectPIMInterface(XDirectPIMInterface self);
uint32_t symbol_offset, uint8_t *ptr_dest, uint32_t length, int id);
void allocate(XDirectPIMInterface* self, uint32_t nr_of_ranks, std::string binary);

void allocate(XDirectPIMInterface* self, uint32_t nr_of_ranks, std::string binary);
void deallocate(XDPI self);
void Launch(XDPI self, bool async);
void sync(XDPI self);
void PrintLog(XDPI self);
void SendToPIM(XDPI self, uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async);
void ReceiveFromPIM(XDPI self, uint8_t** buffers, std::string symbol_name, uint32_t symbol_offset, uint32_t length, bool async);
