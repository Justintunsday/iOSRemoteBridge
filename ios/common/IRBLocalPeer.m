#import "IRBLocalPeer.h"
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
@implementation IRBLocalPeer {
    int _fd;
    uint32_t _sequence;
    dispatch_queue_t _writer;
    NSUInteger _pending;
}
- (instancetype)initWithSocket:(int)fd {
    if((self=[super init])) {
        _fd=fd; _writer=dispatch_queue_create("bridge.local.writer",DISPATCH_QUEUE_SERIAL);
        int one=1; setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
        struct timeval tv={2,0}; setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    } return self;
}
static BOOL transfer(int fd, void *ptr, size_t n, BOOL writing) {
    uint8_t *p=ptr;
    while(n) {
        ssize_t r=writing?send(fd,p,n,0):recv(fd,p,n,0);
        if(r<0&&errno==EINTR) continue;
        if(r<=0) return NO;
        p+=r; n-=(size_t)r;
    } return YES;
}
- (void)start {
    int readFD; @synchronized(self) { readFD=dup(_fd); }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        uint32_t expected=0;
        while(readFD>=0) { @autoreleasepool {
            uint8_t raw[IRB_HEADER_SIZE]; irb_header h;
            if(!transfer(readFD,raw,sizeof(raw),NO)||!irb_unpack(raw,sizeof(raw),&h)||h.sequence!=++expected) break;
            NSMutableData *data=[NSMutableData dataWithLength:h.length];
            if(!transfer(readFD,data.mutableBytes,h.length,NO)) break;
            /* Bound outstanding main-queue deliveries: synchronous handoff is
               local IPC only; the SpringBoard main thread never waits for I/O. */
            dispatch_sync(dispatch_get_main_queue(), ^{ if(self.received) self.received(h,data); });
        }}
        if(readFD>=0) close(readFD);
        [self close];
    });
}
- (void)send:(uint8_t)type flags:(uint8_t)flags pts:(uint64_t)pts data:(NSData *)data {
    if(data.length>IRB_MAX_PAYLOAD) { [self close]; return; }
    @synchronized(self) {
        if(_fd<0) return;
        if(_pending+data.length>IRB_MAX_PAYLOAD+IRB_MAX_RESPONSE) { [self close]; return; }
        _pending+=data.length;
    }
    dispatch_async(_writer, ^{ @autoreleasepool {
        int fd; @synchronized(self) { fd=self->_fd<0?-1:dup(self->_fd); }
        NSData *packet=IRBPacket(type,flags,++self->_sequence,pts,data);
        BOOL ok=fd>=0&&transfer(fd,(void *)packet.bytes,packet.length,YES);
        if(fd>=0) close(fd);
        @synchronized(self) { self->_pending-=data.length; }
        if(!ok) [self close];
    }});
}
- (void)close {
    @synchronized(self) {
        if(_fd<0) return;
        shutdown(_fd,SHUT_RDWR); close(_fd); _fd=-1;
    }
    dispatch_async(dispatch_get_main_queue(), ^{ if(self.disconnected) self.disconnected(); });
}
- (void)dealloc { if(_fd>=0) { shutdown(_fd,SHUT_RDWR); close(_fd); } }
@end
