#import <Foundation/Foundation.h>

// Protocol bounds use UTF-16 code units, matching NSString.length on iOS.
// Reject malformed surrogate pairs rather than silently changing literal text.
static inline BOOL IRBValidLiteralText(id value) {
    if(![value isKindOfClass:NSString.class]||[value length]>1024) return NO;
    NSString *text=value;
    for(NSUInteger i=0;i<text.length;++i) {
        unichar unit=[text characterAtIndex:i];
        if(unit>=0xd800&&unit<=0xdbff) {
            if(++i>=text.length) return NO;
            unit=[text characterAtIndex:i];
            if(unit<0xdc00||unit>0xdfff) return NO;
        } else if(unit>=0xdc00&&unit<=0xdfff) return NO;
    }
    return YES;
}
