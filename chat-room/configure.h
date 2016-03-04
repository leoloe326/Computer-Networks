#define JOIN 2
#define SEND 4
#define FWD 3
#define USERNAME 2
#define MESSAGE 4
#define REASON 1
#define CLIENT_COUNT 3

union attrhdr_t{
    struct{
        unsigned int attrtype : 16;
        unsigned int size : 16;
    }attrfield;
    uint32_t bitstream;
};

union msghdr_t{
    struct{
        unsigned int vrsn : 9;
        unsigned int type : 7;
        unsigned int length : 16;
    }msgfield;
	uint32_t bitstream;
};

union attrpayload_t{
    unsigned char payload[4];
    uint32_t bitstream;
};

