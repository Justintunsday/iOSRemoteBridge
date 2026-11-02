#ifndef IRB_LOCK_POLICY_H
#define IRB_LOCK_POLICY_H
#include <stdint.h>
#include <string.h>
/* Unknown operations are treated as input until schema validation rejects them. */
static inline int irb_operation_is_input(const char *op) {
    const char *read[] = {"get_device_info","get_screen_frame","get_orientation","get_connection_status","get_performance_stats","configure","request_keyframe","disconnect","wake_screen"};
    if(!op) return 1;
    for(unsigned i=0;i<sizeof(read)/sizeof(read[0]);++i) if(strcmp(op,read[i])==0) return 0;
    return 1;
}
static inline int irb_input_permitted(int control,int locked,int negotiated,int epoch_matches) {
    return control&&!locked&&(!negotiated||epoch_matches);
}
static inline int irb_capture_rate(int requested,int locked,int screen_known,int screen_on) {
    if(screen_known&&!screen_on) return 0;
    return locked?5:requested;
}
static inline int irb_frame_visible(uint64_t pts,uint64_t minimum) { return pts&&pts>=minimum; }
#endif
