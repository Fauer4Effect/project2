
#define ReqMessageType 1
#define OkMessageType 2
#define NewViewMessageType 3
#define JoinMessageType 4
#define OpAdd 1

typedef struct {
    uint32_t request_id;
    uint32_t curr_view_id;
    uint32_t op_type;
    uint32_t peer_id;
} ReqMessage;

typedef struct {
    uint32_t request_id;
    uint32_t curr_view_id;
} OkMessage;

typedef struct {
    uint32_t view_id;
    unsigned char *membership_list;
} NewViewMessage;

typedef struct {
    uint32_t process_id;
} JoinMessage;

typedef struct {
    uint32_t msg_type;      // ReqMessage is type 1
                            // OkMessage is type 2
                            // NewViewMessage is type 3
                            // JoinMessage is type 4
    uint32_t size;
} Header;