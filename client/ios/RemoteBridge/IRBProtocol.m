#import "IRBProtocol.h"
@implementation IRBPacket
@end
@implementation IRBPacketReader {
    NSMutableData *_buffer;
    uint32_t _sequence;
    NSString *_error;
}
- (instancetype)init { if((self=[super init])) _buffer=[NSMutableData new]; return self; }
- (NSString *)error { return _error; }
- (NSArray<IRBPacket *> *)append:(NSData *)data {
    if(_error) return @[];
    if(_buffer.length+data.length>IRB_MAX_PAYLOAD+IRB_HEADER_SIZE+65536) { _error=@"接收缓冲区超出限制";return @[]; }
    [_buffer appendData:data]; NSMutableArray *packets=[NSMutableArray new];
    while(_buffer.length>=IRB_HEADER_SIZE) {
        irb_header header;
        if(!irb_unpack(_buffer.bytes,_buffer.length,&header)||
           (header.type!=IRB_VIDEO&&header.type!=IRB_RESPONSE&&header.type!=IRB_PONG)||
           (header.type==IRB_VIDEO&&!header.pts_us)) { _error=@"服务器数据格式不正确";return @[]; }
        if(_buffer.length<IRB_HEADER_SIZE+header.length) break;
        if(header.sequence!=_sequence+1) { _error=@"服务器数据顺序错误";return @[]; }
        ++_sequence;
        IRBPacket *packet=[IRBPacket new]; packet.header=header;
        packet.payload=[_buffer subdataWithRange:NSMakeRange(IRB_HEADER_SIZE,header.length)];
        [packets addObject:packet];
        [_buffer replaceBytesInRange:NSMakeRange(0,IRB_HEADER_SIZE+header.length) withBytes:NULL length:0];
    }
    return packets;
}
@end
NSData *IRBClientPacket(uint8_t type,uint32_t sequence,uint64_t pts,NSData *payload) {
    irb_header header={type,0,(uint32_t)payload.length,sequence,pts};uint8_t bytes[IRB_HEADER_SIZE];irb_pack(bytes,header);
    NSMutableData *packet=[NSMutableData dataWithBytes:bytes length:sizeof(bytes)];if(payload) [packet appendData:payload];return packet;
}
NSDictionary *IRBClientJSON(NSData *data) {
    id value=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    return [value isKindOfClass:NSDictionary.class]?value:nil;
}
