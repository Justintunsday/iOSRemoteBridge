#import <AppKit/AppKit.h>
int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=2) return 2;
    NSBitmapImageRep *bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:1024 pixelsHigh:1024 bitsPerSample:8 samplesPerPixel:3 hasAlpha:NO isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [NSGraphicsContext saveGraphicsState];NSGraphicsContext.currentContext=[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    NSGradient *gradient=[[NSGradient alloc] initWithStartingColor:[NSColor colorWithRed:.04 green:.13 blue:.34 alpha:1] endingColor:[NSColor colorWithRed:.05 green:.48 blue:.83 alpha:1]];
    [gradient drawInRect:NSMakeRect(0,0,1024,1024) angle:45];
    [[NSColor colorWithWhite:1 alpha:.16] setFill];[[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(120,300,610,430) xRadius:48 yRadius:48] fill];
    NSBezierPath *screen=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(145,325,560,380) xRadius:30 yRadius:30];[[NSColor colorWithWhite:1 alpha:.7] setStroke];screen.lineWidth=20;[screen stroke];
    NSBezierPath *stand=[NSBezierPath bezierPath];[stand moveToPoint:NSMakePoint(310,230)];[stand lineToPoint:NSMakePoint(530,230)];stand.lineWidth=28;stand.lineCapStyle=NSLineCapStyleRound;[stand stroke];
    NSBezierPath *phone=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(540,160,315,660) xRadius:58 yRadius:58];[[NSColor colorWithRed:.07 green:.19 blue:.39 alpha:1] setFill];[phone fill];[NSColor.whiteColor setStroke];phone.lineWidth=26;[phone stroke];
    NSBezierPath *bar=[NSBezierPath bezierPath];[bar moveToPoint:NSMakePoint(645,750)];[bar lineToPoint:NSMakePoint(750,750)];bar.lineWidth=18;bar.lineCapStyle=NSLineCapStyleRound;[bar stroke];
    [[NSColor colorWithRed:.35 green:.91 blue:1 alpha:1] setStroke];
    NSBezierPath *arrow=[NSBezierPath bezierPath];[arrow moveToPoint:NSMakePoint(330,525)];[arrow lineToPoint:NSMakePoint(690,525)];[arrow moveToPoint:NSMakePoint(600,615)];[arrow lineToPoint:NSMakePoint(690,525)];[arrow lineToPoint:NSMakePoint(600,435)];arrow.lineWidth=36;arrow.lineCapStyle=NSLineCapStyleRound;arrow.lineJoinStyle=NSLineJoinStyleRound;[arrow stroke];
    [NSGraphicsContext restoreGraphicsState];NSData *png=[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return [png writeToFile:[NSString stringWithUTF8String:argv[1]] atomically:YES]?0:1;
} }
