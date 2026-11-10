#import "App.h"
#import "Palette.h"
#import "Tags.h"
#include <wchar.h>

NSColor *JNColor(COLORREF c) {
    return [NSColor colorWithSRGBRed:GetRValue(c)/255.0 green:GetGValue(c)/255.0 blue:GetBValue(c)/255.0 alpha:1];
}
void JNError(NSError *error) { if (error) [NSApp presentError:error]; }
void JNActivate(void) {
    if (@available(macOS 14.0, *)) [NSApp activate];
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        /* Compatibility with the macOS 13 deployment target. */
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    }
}
NSString *JNAsk(NSString *title, NSString *value) {
    NSAlert *alert = [NSAlert new]; alert.messageText = title;
    [alert addButtonWithTitle:@"Save"]; [alert addButtonWithTitle:@"Cancel"];
    NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,360,24)]; field.stringValue = value;
    alert.accessoryView = field; alert.window.initialFirstResponder = field;
    NSModalResponse response=[alert runModal]; [alert.window makeFirstResponder:nil];
    return response==NSAlertFirstButtonReturn ? field.stringValue : nil;
}
static NSInteger bounded(NSString *value, NSInteger fallback, NSInteger low, NSInteger high) {
    NSInteger number = value ? value.integerValue : fallback;
    return MAX(low, MIN(high, number));
}
NSString *JNPaletteName(NSInteger index) {
    const wchar_t *p = PALETTE_NAMES[index];
    return [[NSString alloc] initWithBytes:p length:wcslen(p)*sizeof(*p) encoding:NSUTF32LittleEndianStringEncoding];
}

NSFont *JNFont(CGFloat size, BOOL bold) {
    return [NSFont monospacedSystemFontOfSize:size weight:bold ? NSFontWeightBold : NSFontWeightRegular];
}
NSRect JNConstrainFrame(NSRect frame, NSRect visible) {
    frame.size.width=MIN(frame.size.width,visible.size.width); frame.size.height=MIN(frame.size.height,visible.size.height);
    frame.origin.x=MAX(NSMinX(visible),MIN(frame.origin.x,NSMaxX(visible)-frame.size.width));
    frame.origin.y=MAX(NSMinY(visible),MIN(frame.origin.y,NSMaxY(visible)-frame.size.height)); return frame;
}
@interface JNBorder : NSView @end
@implementation JNBorder
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }
- (void)drawRect:(NSRect)dirty { (void)dirty; [NSColor.blackColor setStroke]; NSBezierPath *p=[NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds,0.5,0.5)]; p.lineWidth=1; if(self.bounds.size.height>32) { [p moveToPoint:NSMakePoint(1,self.bounds.size.height-32.5)]; [p lineToPoint:NSMakePoint(self.bounds.size.width-1,self.bounds.size.height-32.5)]; } [p stroke]; }
@end
@interface JNBarButton : NSButton
@property(nonatomic) BOOL hovered, folded, assigned;
@property(nonatomic, strong) NSColor *ink;
@end
@implementation JNBarButton { NSTrackingArea *_tracking; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas]; if(_tracking) [self removeTrackingArea:_tracking];
    _tracking=[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited|NSTrackingActiveAlways|NSTrackingInVisibleRect owner:self userInfo:nil]; [self addTrackingArea:_tracking];
}
- (void)mouseEntered:(NSEvent *)e { (void)e; _hovered=YES; self.needsDisplay=YES; }
- (void)mouseExited:(NSEvent *)e { (void)e; _hovered=NO; self.needsDisplay=YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty; if(_hovered) { [(self.tag==2 ? [NSColor colorWithSRGBRed:0.92 green:0.63 blue:0.59 alpha:1] : [_ink colorWithAlphaComponent:0.1]) setFill]; NSRectFill(self.bounds); }
    [(_hovered && self.tag==2 ? NSColor.blackColor : _ink) setStroke];
    NSBezierPath *p=[NSBezierPath bezierPath]; p.lineWidth=1;
    if(self.tag==0) {
        [p appendBezierPathWithRect:NSMakeRect(10,10,12,11)]; [p moveToPoint:NSMakePoint(10,17)]; [p lineToPoint:NSMakePoint(22,17)];
        for(NSInteger x=13;x<=19;x+=6) { [p moveToPoint:NSMakePoint(x,19)]; [p lineToPoint:NSMakePoint(x,23)]; }
        if(_assigned) [p appendBezierPathWithRect:NSMakeRect(13,12,3,3)];
    } else if(self.tag==1 && _folded) [p appendBezierPathWithRect:NSMakeRect(11,12,10,8)];
    else if(self.tag==1) { [p moveToPoint:NSMakePoint(11,16)]; [p lineToPoint:NSMakePoint(21,16)]; }
    else { [p moveToPoint:NSMakePoint(12,12)]; [p lineToPoint:NSMakePoint(20,20)]; [p moveToPoint:NSMakePoint(20,12)]; [p lineToPoint:NSMakePoint(12,20)]; }
    [p stroke];
}
@end

@interface JNPanel : NSPanel @end
@implementation JNPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return NO; }
@end
@interface JNBar : NSView
@property(nonatomic, weak) JNNote *note;
@property(nonatomic, strong) NSColor *paper;
@property(nonatomic, strong) NSColor *ink;
@property(nonatomic, copy) NSString *caption;
@end
@implementation JNBar
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)dirty {
    [self.paper setFill]; NSRectFill(NSIntersectionRect(dirty,self.bounds));
    NSMutableParagraphStyle *style=[NSMutableParagraphStyle new]; style.lineBreakMode=NSLineBreakByTruncatingTail;
    [self.caption drawInRect:NSMakeRect(36,7,MAX(0,self.bounds.size.width-106),22)
        withAttributes:@{NSFontAttributeName:JNFont(14,YES), NSForegroundColorAttributeName:self.ink, NSParagraphStyleAttributeName:style}];
    [NSColor.blackColor setFill]; NSRectFill(NSMakeRect(0,31,self.bounds.size.width,1));
}
- (void)mouseDown:(NSEvent *)event {
    if (event.clickCount == 2) [self.note rename:nil];
    else [self.window performWindowDragWithEvent:event];
}
- (void)rightMouseDown:(NSEvent *)event { [NSMenu popUpContextMenu:self.note.contextMenu withEvent:event forView:self]; }
@end
@interface JNText : NSTextView
@property(nonatomic, weak) JNNote *note;
@property(nonatomic) BOOL pasting, pasteOverflow;
@end
@implementation JNText { CGFloat _wheelAmount, _pinchAmount; NSRange _lastSelection; }
- (void)rightMouseDown:(NSEvent *)event { [self.note showPalette:event]; }
- (void)keyDown:(NSEvent *)event {
    if(event.keyCode==36 || event.keyCode==76) {
        if([self.note insertNewlineWithShift:(event.modifierFlags&NSEventModifierFlagShift)!=0]) return;
    }
    if((event.modifierFlags&NSEventModifierFlagCommand) && ([event.charactersIgnoringModifiers isEqual:@"="] || [event.charactersIgnoringModifiers isEqual:@"+"])) { [self.note resizeBy:1]; return; }
    if(event.keyCode==96) { [self.note.app reload:nil]; return; }
    [super keyDown:event];
}
- (void)scrollWheel:(NSEvent *)event {
    if(!(event.modifierFlags&NSEventModifierFlagCommand)) { [super scrollWheel:event]; return; }
    if(!NSEqualRanges(_lastSelection,self.selectedRange) || event.phase==NSEventPhaseBegan) _wheelAmount=0;
    _lastSelection=self.selectedRange; _wheelAmount+=event.scrollingDeltaY/(event.hasPreciseScrollingDeltas?10:1);
    while(fabs(_wheelAmount)>=1) { CGFloat step=_wheelAmount>0?1:-1; [self.note resizeBy:step]; _wheelAmount-=step; }
}
- (void)magnifyWithEvent:(NSEvent *)event {
    if(!NSEqualRanges(_lastSelection,self.selectedRange) || event.phase==NSEventPhaseBegan) _pinchAmount=0;
    _lastSelection=self.selectedRange; _pinchAmount+=event.magnification*10;
    while(fabs(_pinchAmount)>=1) { CGFloat step=_pinchAmount>0?1:-1; [self.note resizeBy:step]; _pinchAmount-=step; }
}
- (void)paste:(id)sender { [self pasteAsPlainText:sender]; }
- (BOOL)readSelectionFromPasteboard:(NSPasteboard *)pasteboard type:(NSPasteboardType)type {
    self.pasting=YES; self.pasteOverflow=NO;
    BOOL result=[super readSelectionFromPasteboard:pasteboard type:type];
    self.pasting=NO;
    if (self.pasteOverflow) {
        NSAlert *alert=[NSAlert new]; alert.messageText=@"The pasted text does not fit in this note";
        alert.informativeText=@"Enlarge the note, paste less text, or enable Scrollable text from the note bar menu.";
        [alert addButtonWithTitle:@"OK"]; [alert runModal];
    }
    return result;
}
- (void)mouseDown:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSUInteger index = [self characterIndexForInsertionAtPoint:point];
    if (index < self.string.length) {
        unichar c = [self.string characterAtIndex:index];
        if (c == 0x2610 || c == 0x2611) {
            NSRect glyph = [self firstRectForCharacterRange:NSMakeRange(index,1) actualRange:NULL];
            NSPoint screen = [self.window convertPointToScreen:event.locationInWindow];
            if (NSPointInRect(screen,glyph)) { [self.note toggleChecklistAt:index]; return; }
        }
    }
    [super mouseDown:event];
}
@end

@implementation JNNote {
    JNBar *_bar;
    NSScrollView *_scroll;
    NSTimer *_saveTimer;
    BOOL _dirty, _contentDirty, _formatDirty, _layoutDirty, _ready, _collapsed;
    CGFloat _expandedHeight;
    NSInteger _palette, _color;
    BOOL _customInk, _saveFailed, _scrolling;
    JNPalette *_picker;
    JNBarButton *_foldButton, *_dateButton;
    NSTextStorage *_measureText;
    NSLayoutManager *_measureLayout;
    NSTextContainer *_measureContainer;
}
- (BOOL)scrolling { return _scrolling; }
- (BOOL)collapsed { return _collapsed; }
- (instancetype)initWithRecord:(JNRecord *)record app:(JNApp *)app fresh:(BOOL)fresh error:(NSError **)error {
    NSDictionary *s = record.settings;
    CGFloat width = bounded(s[@"mac_width"] ?: s[@"width"],300,180,4096);
    CGFloat height = bounded(s[@"mac_height"] ?: s[@"height"],240,100,4096);
    NSRect frame = NSMakeRect(120 + app.notes.count*24,120 + app.notes.count*24,width,height);
    if (s[@"mac_x"] && s[@"mac_y"]) frame.origin = NSMakePoint([s[@"mac_x"] doubleValue],[s[@"mac_y"] doubleValue]);
    JNPanel *panel = [[JNPanel alloc] initWithContentRect:frame styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    if (!(self = [super initWithWindow:panel])) return nil;
    _record = record; _app = app; _expandedHeight = height;
    _scrolling=[(fresh ? app.store.preferences[@"scroll"] : s[@"scroll"]) boolValue];
    _palette = bounded(s[@"palette"],0,0,PALETTE_COUNT-1);
    _color = bounded(s[@"color"],0,0,PALETTE_SIZES[_palette]-1);
    _customInk = [s[@"custom_ink"] boolValue];
    panel.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua]; panel.title = @"Jotilla"; panel.hasShadow=YES; panel.level = NSFloatingWindowLevel;
    panel.hidesOnDeactivate = NO; panel.releasedWhenClosed = NO; panel.delegate = self;
    panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    panel.minSize = NSMakeSize(180,100); panel.alphaValue = bounded(s[@"opacity"],100,60,100)/100.0;
    _bar = [[JNBar alloc] initWithFrame:NSMakeRect(0,height-32,width,32)];
    _bar.note = self; _bar.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [panel.contentView addSubview:_bar];
    NSArray *actions = @[NSStringFromSelector(@selector(chooseDate:)),NSStringFromSelector(@selector(fold:)),NSStringFromSelector(@selector(close))];
    for (NSUInteger i=0; i<3; ++i) {
        JNBarButton *button = [[JNBarButton alloc] initWithFrame:NSMakeRect(i==0 ? 0 : width-(3-i)*32,0,32,32)];
        button.target=self; button.action=NSSelectorFromString(actions[i]); button.tag=(NSInteger)i; button.bordered=NO;
        button.autoresizingMask = i==0 ? NSViewMaxXMargin : NSViewMinXMargin;
        button.toolTip = @[@"Assign a day",@"Fold or expand",@"Close and keep in archive"][i];
        [button setAccessibilityLabel:button.toolTip]; [_bar addSubview:button];
        if(i==0) _dateButton=button; if(i==1) _foldButton=button;
    }
    _scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(1,1,width-2,height-33)];
    _scroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; _scroll.hasVerticalScroller = NO;
    JNText *editor = [[JNText alloc] initWithFrame:_scroll.contentView.bounds]; editor.note = self;
    editor.minSize = NSMakeSize(0,0); editor.maxSize = NSMakeSize(CGFLOAT_MAX,CGFLOAT_MAX);
    editor.verticallyResizable = YES; editor.horizontallyResizable = NO;
    editor.autoresizingMask = NSViewWidthSizable;
    editor.textContainer.containerSize = NSMakeSize(width,CGFLOAT_MAX); editor.textContainer.widthTracksTextView = YES;
    editor.textContainerInset = NSMakeSize(10,8); editor.richText = YES; editor.importsGraphics = NO;
    editor.allowsUndo = YES; editor.delegate = self;
    editor.automaticQuoteSubstitutionEnabled = NO; editor.automaticDashSubstitutionEnabled = NO;
    _editor = editor; _scroll.documentView = editor; [panel.contentView addSubview:_scroll];
    NSAttributedString *text = fresh ? [[NSAttributedString alloc] initWithString:@""] : [app.store read:record error:error];
    if (!text) return nil;
    [editor.textStorage setAttributedString:text]; if (!text.length) editor.font = JNFont(14,YES);
    JNBorder *border=[[JNBorder alloc] initWithFrame:panel.contentView.bounds]; border.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable; [panel.contentView addSubview:border];
    [self applyTheme]; [editor.undoManager removeAllActions];
    NSRect visible = (NSScreen.mainScreen ?: NSScreen.screens.firstObject).visibleFrame;
    for (NSScreen *screen in NSScreen.screens) if (NSIntersectsRect(NSMakeRect(frame.origin.x,NSMaxY(frame)-32,width,32),screen.visibleFrame)) { visible = screen.visibleFrame; break; }
    frame.size.width = MIN(frame.size.width,visible.size.width); frame.size.height = MIN(frame.size.height,visible.size.height);
    frame.origin.x = MAX(NSMinX(visible),MIN(frame.origin.x,NSMaxX(visible)-frame.size.width));
    frame.origin.y = MAX(NSMinY(visible),MIN(frame.origin.y,NSMaxY(visible)-frame.size.height));
    [panel setFrame:frame display:NO]; _expandedHeight = frame.size.height;
    if ([s[@"collapsed"] boolValue]) [self fold:nil];
    /* Native scrolling needs no scroller or reserved gutter, including for
       imported notes whose text is taller than this display. */
    _scroll.hasVerticalScroller=NO; _scroll.hasHorizontalScroller=NO;
    _ready = YES; _layoutDirty = fresh;
    return self;
}
- (void)dealloc { [_saveTimer invalidate]; }
- (void)detach {
    [_picker close];
    _ready = NO; [_saveTimer invalidate]; _saveTimer = nil;
    [_editor.undoManager removeAllActions]; _editor.delegate = nil; self.window.delegate = nil;
    [self.window orderOut:nil];
}
- (void)applyTheme {
    NSColor *paper = JNColor(PALETTES[_palette][_color]), *ink = JNColor(palette_ink((int)_palette,(int)_color));
    self.window.backgroundColor = paper; _editor.backgroundColor = paper; _scroll.backgroundColor = paper;
    if (!_customInk) {
        [_editor.undoManager disableUndoRegistration];
        [_editor.textStorage addAttribute:NSForegroundColorAttributeName value:ink range:NSMakeRange(0,_editor.string.length)];
        [_editor.undoManager enableUndoRegistration];
    }
    _editor.insertionPointColor = ink;
    NSMutableDictionary *typing = [_editor.typingAttributes mutableCopy];
    if (!_customInk) typing[NSForegroundColorAttributeName] = ink;
    if (!typing[NSFontAttributeName]) typing[NSFontAttributeName] = JNFont(14,YES);
    _editor.typingAttributes = typing;
    _bar.paper = JNColor(_saveFailed ? RGB(235,161,151) : palette_bar(PALETTES[_palette][_color]));
    _bar.ink = _saveFailed ? NSColor.blackColor : JNColor(palette_bar_ink((int)_palette,(int)_color));
    _record.settings[@"palette"]=@(_palette).stringValue; _record.settings[@"color"]=@(_color).stringValue;
    self.window.title=_record.title.length?_record.title:@"Jotilla";
    _bar.caption = _saveFailed ? @"Save failed — retry from menu" : _record.title; _bar.needsDisplay = YES;
    for (JNBarButton *button in _bar.subviews) { button.ink=_bar.ink; button.needsDisplay=YES; }
    _foldButton.folded=_collapsed; _dateButton.assigned=_record.day!=0;
    _dateButton.toolTip=_record.day ? [NSString stringWithFormat:@"%04lu-%02lu-%02lu — change day",(unsigned long)(_record.day/10000),(unsigned long)(_record.day/100%100),(unsigned long)(_record.day%100)] : @"Assign a day";
}
- (void)schedule {
    [_saveTimer invalidate];
    __weak JNNote *weakSelf = self;
    _saveTimer = [NSTimer scheduledTimerWithTimeInterval:0.4 repeats:NO block:^(NSTimer *timer) {
        (void)timer; NSError *error = nil; JNNote *note = weakSelf;
        if (note && ![note flush:&error]) { note->_bar.caption = @"Save failed — retry from menu"; note->_bar.needsDisplay = YES; JNError(error); }
    }];
}
- (void)textDidChange:(NSNotification *)notification { (void)notification; if (_ready) { _dirty = _contentDirty = _formatDirty = YES; [self schedule]; } }
- (void)windowDidBecomeKey:(NSNotification *)notification { (void)notification; if (_ready) [self.app rememberNote:self]; }
- (void)windowDidResignKey:(NSNotification *)notification { (void)notification; if(_ready) { NSError *error=nil; if(![self flush:&error]) JNError(error); } }
- (void)windowDidChangeScreen:(NSNotification *)notification { (void)notification; [_picker close]; }
- (void)windowDidResize:(NSNotification *)notification { (void)notification; [self geometryChanged]; }
- (void)windowDidMove:(NSNotification *)notification { (void)notification; [self geometryChanged]; }
- (void)geometryChanged { if (_ready) { _layoutDirty = YES; [self schedule]; } }
- (BOOL)flush:(NSError **)error {
    [_saveTimer invalidate]; _saveTimer = nil;
    if (!_dirty && !_layoutDirty) return YES;
    NSRect frame = self.window.frame;
    if (!_collapsed) _expandedHeight = frame.size.height;
    NSMutableDictionary *s = _record.settings;
    s[@"mac_x"] = @(frame.origin.x).stringValue; s[@"mac_y"] = @(frame.origin.y).stringValue;
    s[@"mac_width"] = @(frame.size.width).stringValue; s[@"mac_height"] = @(_expandedHeight).stringValue;
    s[@"open"] = @"1"; s[@"collapsed"] = _collapsed ? @"1" : @"0";
    s[@"palette"] = @(_palette).stringValue; s[@"color"] = @(_color).stringValue;
    s[@"custom_ink"] = _customInk ? @"1" : @"0";
    s[@"scroll"] = _scrolling ? @"1" : @"0";
    s[@"opacity"] = @((int)round(self.window.alphaValue*100)).stringValue;
    BOOL exists = [NSFileManager.defaultManager fileExistsAtPath:[self.app.store URLFor:_record extension:@"txt"].path];
    BOOL ok = _dirty ? [self.app.store save:_record text:_editor.textStorage writeText:_contentDirty writeRich:_formatDirty error:error] : !exists || [self.app.store saveMetadata:error];
    _saveFailed=!ok;
    if(!ok) [self applyTheme];
    if (ok) { BOOL changed = _dirty; _dirty = _contentDirty = _formatDirty = _layoutDirty = NO; [self applyTheme]; if (changed) [self.app changedRecord:_record]; }
    return ok;
}
- (void)showAtPointer:(BOOL)pointer activate:(BOOL)activate {
    if (pointer) {
        NSPoint mouse = NSEvent.mouseLocation; NSRect f = self.window.frame;
        NSScreen *screen = NSScreen.mainScreen;
        for (NSScreen *candidate in NSScreen.screens) if (NSPointInRect(mouse,candidate.frame)) { screen = candidate; break; }
        NSRect v = screen.visibleFrame;
        f.origin = NSMakePoint(MAX(NSMinX(v),MIN(mouse.x,NSMaxX(v)-f.size.width)),MAX(NSMinY(v),MIN(mouse.y-f.size.height,NSMaxY(v)-f.size.height)));
        [self.window setFrame:f display:NO];
    }
    if (activate) { [self.app rememberNote:self]; JNActivate(); [self.window makeKeyAndOrderFront:nil]; [self.window makeFirstResponder:_editor]; }
    else [self.window orderFront:nil];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender; NSError *error = nil;
    if (![self flush:&error]) { JNError(error); return NO; }
    _record.settings[@"open"] = @"0";
    if (![self.app.store saveMetadata:&error]) { _record.settings[@"open"] = @"1"; JNError(error); return NO; }
    [self detach]; [self.app closeNote:self]; return YES;
}
- (void)close { if ([self windowShouldClose:self.window]) [self.window close]; }
- (void)fold:(id)sender {
    (void)sender; NSRect f = self.window.frame;
    if (!_collapsed) _expandedHeight = f.size.height;
    CGFloat height = _collapsed ? _expandedHeight : 32;
    f.origin.y += f.size.height-height; f.size.height = height; _collapsed = !_collapsed;
    _scroll.hidden = _collapsed; self.window.minSize = NSMakeSize(180,_collapsed ? 32 : 100);
    [self.window setFrame:f display:YES]; [self applyTheme]; [self geometryChanged];
}
- (void)rename:(id)sender {
    (void)sender; NSString *title = JNAsk(@"Note title",_record.title);
    if (!title) return;
    [self setTitleValue:[[JNPlain(title) componentsSeparatedByString:@"\n"] componentsJoinedByString:@" "]];
    _dirty = _contentDirty = YES; [self applyTheme]; [self schedule];
}
- (void)tags:(id)sender {
    (void)sender; NSString *input = JNEditTags(_record,self.app.store);
    if (!input) return; NSError *error = nil;
    NSString *tags = JNNormalizeTags(input,&error);
    if (!tags) { JNError(error); return; }
    _record.tags = tags; _dirty = YES; [self schedule];
}
- (void)chooseDate:(id)sender {
    (void)sender;
    if(![_record.title stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].length && ![_editor.string stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].length) { JNError([NSError errorWithDomain:@"Jotilla" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Add a title or some text before assigning a day."}]); return; }
    NSAlert *alert = [NSAlert new]; alert.messageText = @"Assign a day";
    [alert addButtonWithTitle:@"Save"]; [alert addButtonWithTitle:@"Remove day"]; [alert addButtonWithTitle:@"Cancel"];
    NSDatePicker *picker = [[NSDatePicker alloc] initWithFrame:NSMakeRect(0,0,300,170)];
    picker.datePickerStyle = NSDatePickerStyleClockAndCalendar; picker.datePickerElements = NSDatePickerElementFlagYearMonthDay;
    NSCalendar *calendar = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    picker.calendar = calendar;
    if (_record.day) { NSDateComponents *c = [NSDateComponents new]; c.year = _record.day/10000; c.month = _record.day/100%100; c.day = _record.day%100; picker.dateValue = [calendar dateFromComponents:c]; }
    else picker.dateValue = NSDate.date;
    NSView *accessory=[[NSView alloc] initWithFrame:NSMakeRect(0,0,300,210)]; [accessory addSubview:picker];
    NSDatePicker *typed=[[NSDatePicker alloc] initWithFrame:NSMakeRect(30,180,240,26)]; typed.datePickerStyle=NSDatePickerStyleTextFieldAndStepper; typed.datePickerElements=NSDatePickerElementFlagYearMonthDay; typed.calendar=calendar;
    [typed bind:NSValueBinding toObject:picker withKeyPath:@"dateValue" options:nil]; [accessory addSubview:typed];
    alert.accessoryView = accessory; NSModalResponse response = [alert runModal]; [alert.window makeFirstResponder:nil]; [typed unbind:NSValueBinding];
    if (response == NSAlertThirdButtonReturn) return;
    NSDateComponents *c = [calendar components:NSCalendarUnitYear|NSCalendarUnitMonth|NSCalendarUnitDay fromDate:picker.dateValue];
    unsigned day = response == NSAlertSecondButtonReturn ? 0 : (unsigned)(c.year*10000+c.month*100+c.day);
    if (day && !date_valid(day)) { NSBeep(); return; }
    _record.day = day; _dirty = YES; [self schedule];
}
- (void)replaceRange:(NSRange)range with:(NSAttributedString *)replacement {
    NSAttributedString *before = [_editor.textStorage attributedSubstringFromRange:range];
    NSUInteger nextLength = replacement.length;
    if(!_scrolling && ![_editor.undoManager isUndoing] && ![_editor.undoManager isRedoing]) {
        NSSize size=NSMakeSize(self.window.frame.size.width,_collapsed ? _expandedHeight : self.window.frame.size.height);
        [self prepareMeasure]; [_measureText setAttributedString:_editor.textStorage]; CGFloat beforeHeight=[self measuredHeightAtWidth:size.width];
        NSMutableAttributedString *candidate=[_editor.textStorage mutableCopy]; [candidate replaceCharactersInRange:range withAttributedString:replacement];
        [_measureText setAttributedString:candidate]; CGFloat afterHeight=[self measuredHeightAtWidth:size.width];
        if(afterHeight>size.height+0.5 && afterHeight>=beforeHeight && candidate.length>=_editor.string.length) { NSBeep(); return; }
    }
    [[_editor.undoManager prepareWithInvocationTarget:self] replaceRange:NSMakeRange(range.location,nextLength) with:before];
    [_editor.textStorage replaceCharactersInRange:range withAttributedString:replacement]; _measureText=nil;
    [_editor setSelectedRange:NSMakeRange(range.location,nextLength)];
    _dirty = _formatDirty = YES; if(![before.string isEqual:replacement.string]) _contentDirty=YES; [self schedule];
}
- (void)format:(id)sender {
    if(_collapsed) return;
    NSRange range = _editor.selectedRange; NSInteger command = [sender tag];
    NSMutableAttributedString *selection = range.length ? [[_editor.textStorage attributedSubstringFromRange:range] mutableCopy] : [[NSMutableAttributedString alloc] initWithString:@" " attributes:_editor.typingAttributes];
    if (command == 3 || command == 4) {
        NSString *key = command == 3 ? NSUnderlineStyleAttributeName : NSStrikethroughStyleAttributeName;
        NSInteger value = [[selection attribute:key atIndex:0 effectiveRange:NULL] integerValue] ? 0 : NSUnderlineStyleSingle;
        [selection addAttribute:key value:@(value) range:NSMakeRange(0,selection.length)];
    } else {
        NSFontManager *manager = NSFontManager.sharedFontManager; NSFontTraitMask trait = command == 1 ? NSBoldFontMask : NSItalicFontMask;
        NSFont *first = [selection attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL] ?: JNFont(14,YES);
        BOOL remove = ([manager traitsOfFont:first] & trait) != 0;
        [selection enumerateAttribute:NSFontAttributeName inRange:NSMakeRange(0,selection.length) options:0 usingBlock:^(NSFont *font, NSRange run, BOOL *stop) {
            (void)stop; font = font ?: first;
            [selection addAttribute:NSFontAttributeName value:remove ? [manager convertFont:font toNotHaveTrait:trait] : [manager convertFont:font toHaveTrait:trait] range:run];
        }];
    }
    if(range.length) [self replaceRange:range with:selection]; else [self setTypingStyle:[selection attributesAtIndex:0 effectiveRange:NULL]];
}
- (void)setTypingStyle:(NSDictionary *)attributes { [[_editor.undoManager prepareWithInvocationTarget:self] setTypingStyle:_editor.typingAttributes]; _editor.typingAttributes=attributes; }
- (void)resizeBy:(CGFloat)amount {
    NSRange range = _collapsed ? NSMakeRange(0,0) : _editor.selectedRange;
    if (range.length) {
        NSMutableAttributedString *text = [[_editor.textStorage attributedSubstringFromRange:range] mutableCopy];
        [text enumerateAttribute:NSFontAttributeName inRange:NSMakeRange(0,text.length) options:0 usingBlock:^(NSFont *font, NSRange run, BOOL *stop) {
            (void)stop; font = font ?: JNFont(14,YES);
            [text addAttribute:NSFontAttributeName value:[NSFontManager.sharedFontManager convertFont:font toSize:MAX(6,MIN(144,font.pointSize+amount*2))] range:run];
        }];
        [self replaceRange:range with:text];
    } else {
        NSSize size=self.window.frame.size; if(_collapsed) size.height=_expandedHeight;
        CGFloat factor=amount>0?1.1:1/1.1;
        NSSize available=(self.window.screen ?: NSScreen.mainScreen).visibleFrame.size; factor=MIN(factor,MIN(available.width/size.width,available.height/size.height));
        [self resizeTo:NSMakeSize(size.width*factor,size.height*factor)];
    }
}
- (void)setTitleValue:(NSString *)title {
    [[_editor.undoManager prepareWithInvocationTarget:self] setTitleValue:_record.title];
    _record.title=title; _dirty=_contentDirty=YES; [self applyTheme]; [self schedule];
}
- (BOOL)insertNewlineWithShift:(BOOL)shift {
    NSTextView *text=_editor; NSRange selection=text.selectedRange;
    if(selection.length) return NO;
    NSRange line=[text.string lineRangeForRange:selection];
    NSString *whole=[[text.string substringWithRange:line] stringByTrimmingCharactersInSet:NSCharacterSet.newlineCharacterSet];
    NSString *prefix=[text.string substringWithRange:NSMakeRange(line.location,selection.location-line.location)];
    if(shift && line.location==0 && [whole hasPrefix:@"#"] && whole.length>1) {
        [text breakUndoCoalescing]; [text.undoManager beginUndoGrouping]; [self setTitleValue:[whole substringFromIndex:1]];
        [self replaceRange:line with:[[NSAttributedString alloc] initWithString:@""]];
        [text.undoManager endUndoGrouping]; text.selectedRange=NSMakeRange(0,0); return YES;
    }
    BOOL start=shift && [whole isEqual:@"*"] && selection.location==NSMaxRange(line);
    BOOL todo=[whole hasPrefix:@"☐ "] || [whole hasPrefix:@"☑ "];
    if(!start && (!todo || prefix.length<2)) return NO;
    NSMutableDictionary *attributes=[text.typingAttributes mutableCopy]; [attributes removeObjectForKey:NSStrikethroughStyleAttributeName];
    NSRange range=start || (todo && whole.length==2) ? NSMakeRange(line.location,whole.length) : selection;
    NSString *value=start ? @"☐ " : whole.length==2 ? @"" : @"\n☐ ";
    [text breakUndoCoalescing]; [self replaceRange:range with:[[NSAttributedString alloc] initWithString:value attributes:attributes]];
    text.selectedRange=NSMakeRange(range.location+value.length,0); text.typingAttributes=attributes; return YES;
}
- (BOOL)textView:(NSTextView *)text doCommandBySelector:(SEL)selector {
    if(selector==@selector(insertNewline:) || selector==@selector(insertLineBreak:)) return [self insertNewlineWithShift:selector==@selector(insertLineBreak:)];
    if(selector==@selector(deleteBackward:) && !text.selectedRange.length) {
        NSRange line=[text.string lineRangeForRange:text.selectedRange];
        if(text.selectedRange.location==line.location+2 && line.length>=2) {
            NSString *prefix=[text.string substringWithRange:NSMakeRange(line.location,2)];
            if([prefix isEqual:@"☐ "] || [prefix isEqual:@"☑ "]) {
                [self replaceRange:NSMakeRange(line.location,2) with:[[NSAttributedString alloc] initWithString:@""]]; text.selectedRange=NSMakeRange(line.location,0);
                NSMutableDictionary *attrs=[text.typingAttributes mutableCopy]; [attrs removeObjectForKey:NSStrikethroughStyleAttributeName]; text.typingAttributes=attrs; return YES;
            }
        }
    }
    return NO;
}
- (void)toggleChecklistAt:(NSUInteger)index {
    if(index>=_editor.string.length) return;
    NSRange line=[_editor.string lineRangeForRange:NSMakeRange(index,0)];
    if(index!=line.location || line.length<2) return;
    NSString *prefix=[_editor.string substringWithRange:NSMakeRange(index,2)];
    if(![prefix isEqual:@"☐ "] && ![prefix isEqual:@"☑ "]) return;
    NSRange selection=_editor.selectedRange;
    NSMutableAttributedString *replacement=[[_editor.textStorage attributedSubstringFromRange:line] mutableCopy];
    BOOL checked=[prefix isEqual:@"☐ "];
    [replacement replaceCharactersInRange:NSMakeRange(0,1) withString:checked ? @"☑" : @"☐"];
    [replacement removeAttribute:NSStrikethroughStyleAttributeName range:NSMakeRange(0,replacement.length)];
    if(checked && replacement.length>2) [replacement addAttribute:NSStrikethroughStyleAttributeName value:@(NSUnderlineStyleSingle) range:NSMakeRange(2,replacement.length-2)];
    [_editor breakUndoCoalescing]; [self replaceRange:line with:replacement]; _editor.selectedRange=selection;
}
- (void)prepareMeasure {
    if(_measureText) return;
    _measureText=[[NSTextStorage alloc] initWithAttributedString:_editor.textStorage]; _measureLayout=[NSLayoutManager new];
    _measureContainer=[[NSTextContainer alloc] initWithContainerSize:NSMakeSize(100,CGFLOAT_MAX)];
    _measureContainer.lineFragmentPadding=_editor.textContainer.lineFragmentPadding;
    [_measureText addLayoutManager:_measureLayout]; [_measureLayout addTextContainer:_measureContainer];
}
- (CGFloat)measuredHeightAtWidth:(CGFloat)width {
    _measureContainer.containerSize=NSMakeSize(MAX(1,width-2-2*_editor.textContainerInset.width),CGFLOAT_MAX);
    [_measureLayout ensureLayoutForTextContainer:_measureContainer];
    CGFloat height=NSMaxY([_measureLayout usedRectForTextContainer:_measureContainer]);
    if(_measureLayout.extraLineFragmentTextContainer) height=MAX(height,NSMaxY(_measureLayout.extraLineFragmentRect));
    return height+2*_editor.textContainerInset.height+33;
}
- (BOOL)fitsText:(NSAttributedString *)text size:(NSSize)size {
    [self prepareMeasure]; [_measureText setAttributedString:text]; return [self measuredHeightAtWidth:size.width]<=size.height+0.5;
}
- (BOOL)textView:(NSTextView *)text shouldChangeTextInRange:(NSRange)range replacementString:(NSString *)replacement {
    if(!replacement || !_ready || _scrolling || text.hasMarkedText) return YES;
    [self prepareMeasure];
    if(![_measureText.string isEqual:text.string]) [_measureText setAttributedString:text.textStorage];
    CGFloat available=_collapsed ? _expandedHeight : self.window.frame.size.height;
    CGFloat before=[self measuredHeightAtWidth:self.window.frame.size.width];
    NSAttributedString *previous=[_measureText attributedSubstringFromRange:range];
    [_measureText replaceCharactersInRange:range withAttributedString:[[NSAttributedString alloc] initWithString:replacement attributes:text.typingAttributes]];
    CGFloat after=[self measuredHeightAtWidth:self.window.frame.size.width];
    if(after<=available+0.5 || after<before || replacement.length<range.length) return YES;
    [_measureText replaceCharactersInRange:NSMakeRange(range.location,replacement.length) withAttributedString:previous];
    JNText *editor=(JNText *)text;
    if (editor.pasting) editor.pasteOverflow=YES; else NSBeep();
    return NO;
}
- (BOOL)resizeTo:(NSSize)size {
    NSRect visible=(self.window.screen ?: NSScreen.mainScreen).visibleFrame;
    size.width=MIN(size.width,visible.size.width); size.height=MIN(size.height,visible.size.height);
    if(size.width<180 || size.height<100 || (!_scrolling && ![self fitsText:_editor.textStorage size:size])) return NO;
    _expandedHeight=size.height;
    NSRect frame=self.window.frame; CGFloat height=_collapsed?32:size.height;
    frame.origin.y+=frame.size.height-height; frame.size=NSMakeSize(size.width,height);
    [self.window setFrame:JNConstrainFrame(frame,visible) display:YES]; [self geometryChanged]; return YES;
}
- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)size {
    if(_collapsed) { size.height=32; return size; }
    return _scrolling || [self fitsText:_editor.textStorage size:size] || (size.width>=sender.frame.size.width && size.height>=sender.frame.size.height) ? size : sender.frame.size;
}
- (void)toggleScrolling:(id)sender {
    (void)sender;
    BOOL enabled=!_scrolling;
    NSSize size=NSMakeSize(self.window.frame.size.width,_collapsed ? _expandedHeight : self.window.frame.size.height);
    if (!enabled && ![self fitsText:_editor.textStorage size:size]) {
        NSAlert *alert=[NSAlert new]; alert.messageText=@"The text does not fit without scrolling";
        alert.informativeText=@"Enlarge the note or shorten its text before turning scrolling off.";
        [alert addButtonWithTitle:@"OK"]; [alert runModal]; return;
    }
    NSAlert *alert=[NSAlert new]; alert.messageText=enabled ? @"Enable scrollable text" : @"Disable scrollable text";
    alert.informativeText=@"Apply this to the current note, or also use it as the saved default for new notes. Other existing notes keep their setting.";
    [alert addButtonWithTitle:@"Only this note"]; [alert addButtonWithTitle:@"This note and new notes"]; [alert addButtonWithTitle:@"Cancel"];
    NSModalResponse response=[alert runModal];
    if (response==NSAlertThirdButtonReturn) return;
    NSError *error=nil;
    if (![self flush:&error]) { JNError(error); return; }
    NSString *previous=_record.settings[@"scroll"], *previousDefault=self.app.store.preferences[@"scroll"];
    _record.settings[@"scroll"]=enabled ? @"1" : @"0";
    if (response==NSAlertSecondButtonReturn) self.app.store.preferences[@"scroll"]=_record.settings[@"scroll"];
    if (![self.app.store saveMetadata:&error]) {
        if (previous) _record.settings[@"scroll"]=previous; else [_record.settings removeObjectForKey:@"scroll"];
        if (previousDefault) self.app.store.preferences[@"scroll"]=previousDefault; else [self.app.store.preferences removeObjectForKey:@"scroll"];
        JNError(error); return;
    }
    _scrolling=enabled;
    [_editor scrollRangeToVisible:_editor.selectedRange];
}
- (void)showPalette:(id)sender {
    [_picker close]; _picker=[[JNPalette alloc] initWithNote:self];
    NSPoint point=[sender isKindOfClass:NSEvent.class] ? [self.window convertPointToScreen:[sender locationInWindow]] : NSMakePoint(NSMidX(self.window.frame),NSMaxY(self.window.frame)-80);
    [_picker showAtPoint:point];
}
- (void)presetSize:(NSMenuItem *)sender {
    NSSize sizes[]={ {240,180},{300,240},{420,340},{540,440},{300,300} };
    if(sender.tag>=0 && sender.tag<5 && ![self resizeTo:sizes[sender.tag]]) NSBeep();
}
- (void)selectColor:(NSMenuItem *)item {
    NSInteger palette = item.tag/COLOR_COUNT, color = item.tag%COLOR_COUNT;
    if(palette<0 || palette>=PALETTE_COUNT || color<0 || color>=PALETTE_SIZES[palette]) return;
    NSRange range = _editor.selectedRange;
    if (range.length) {
        NSMutableAttributedString *text = [[_editor.textStorage attributedSubstringFromRange:range] mutableCopy];
        [text addAttribute:NSForegroundColorAttributeName value:JNColor(PALETTES[palette][color]) range:NSMakeRange(0,text.length)];
        _customInk = YES; [self replaceRange:range with:text];
    } else { [self.app usePalette:palette]; _palette = palette; _color = color; [self applyTheme]; _dirty = _layoutDirty = YES; if (!_customInk) _formatDirty = YES; [self schedule]; }
}
- (void)favorite:(NSMenuItem *)item {
    NSInteger palette = item.tag;
    if(palette < -1 || palette>=PALETTE_COUNT) return;
    NSString *previous = self.app.store.preferences[@"favorite_palette"];
    self.app.store.preferences[@"favorite_palette"] = @(palette).stringValue;
    NSError *error = nil;
    if (![self.app.store saveMetadata:&error]) {
        if (previous) self.app.store.preferences[@"favorite_palette"] = previous;
        else [self.app.store.preferences removeObjectForKey:@"favorite_palette"];
        JNError(error); return;
    }
    if (palette >= 0) { [self.app usePalette:palette]; if (_palette != palette) _color = 0; _palette = palette; _dirty = _layoutDirty = YES; if (!_customInk) _formatDirty = YES; [self applyTheme]; [self schedule]; }
}
- (void)opacity:(NSMenuItem *)item { self.window.alphaValue = item.tag/100.0; [self geometryChanged]; }
- (void)retry:(id)sender { (void)sender; NSError *error = nil; if (![self flush:&error]) JNError(error); }
- (void)exportCopy:(id)sender {
    (void)sender; NSSavePanel *panel = [NSSavePanel savePanel]; panel.nameFieldStringValue = @"Note copy.rtf";
    if ([panel runModal] != NSModalResponseOK) return;
    NSData *data = [_editor.textStorage RTFFromRange:NSMakeRange(0,_editor.string.length) documentAttributes:@{}];
    NSError *error = nil; if (!data || !JNWrite(data,panel.URL,&error)) JNError(error);
}
- (NSMenu *)contextMenu {
    NSMenu *menu = [NSMenu new];
    NSArray *names = @[@"Rename…",@"Tags…",@"Assign day…",@"Fold / expand",@"Save now",@"Export an RTF copy…"];
    SEL actions[] = {@selector(rename:),@selector(tags:),@selector(chooseDate:),@selector(fold:),@selector(retry:),@selector(exportCopy:)};
    for (NSUInteger i=0;i<names.count;++i) { NSMenuItem *item = [menu addItemWithTitle:names[i] action:actions[i] keyEquivalent:@""]; item.target = self; }
    NSMenuItem *scrolling=[menu addItemWithTitle:@"Scrollable text" action:@selector(toggleScrolling:) keyEquivalent:@""];
    scrolling.target=self; scrolling.state=_scrolling ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:NSMenuItem.separatorItem];
    NSArray *styles = @[@"Bold",@"Italic",@"Underline",@"Strikethrough"];
    for (NSUInteger i=0;i<styles.count;++i) { NSMenuItem *item = [menu addItemWithTitle:styles[i] action:@selector(format:) keyEquivalent:@""]; item.target = self; item.tag = (NSInteger)i+1; }
    NSMenuItem *wheel=[menu addItemWithTitle:@"Color palette…" action:@selector(showPalette:) keyEquivalent:@""]; wheel.target=self;
    NSMenu *sizes=[NSMenu new]; [menu addItemWithTitle:@"Note size" action:nil keyEquivalent:@""].submenu=sizes;
    NSArray *sizeNames=@[@"Small",@"Medium",@"Large",@"Extra large",@"Square"];
    for(NSInteger i=0;i<5;++i) { NSMenuItem *item=[sizes addItemWithTitle:sizeNames[i] action:@selector(presetSize:) keyEquivalent:@""]; item.target=self; item.tag=i; }
    NSMenu *defaults=[NSMenu new]; [menu addItemWithTitle:@"New note palette" action:nil keyEquivalent:@""].submenu=defaults;
    for(NSInteger p=0;p<DEFAULT_PALETTE_COUNT;++p) { NSMenuItem *item=[defaults addItemWithTitle:JNPaletteName(p) action:@selector(defaultPalette:) keyEquivalent:@""]; item.target=self.app; item.tag=p; item.state=self.app.sessionPalette==p?NSControlStateValueOn:NSControlStateValueOff; }
    NSMenu *opacity = [NSMenu new]; [menu addItemWithTitle:@"Opacity" action:nil keyEquivalent:@""].submenu = opacity;
    for (NSInteger percent=100;percent>=60;percent-=10) { NSMenuItem *item = [opacity addItemWithTitle:[NSString stringWithFormat:@"%ld%%",(long)percent] action:@selector(opacity:) keyEquivalent:@""]; item.target = self; item.tag = percent; }
    NSMenuItem *reveal=[menu addItemWithTitle:@"Open notes folder" action:@selector(revealArchive:) keyEquivalent:@""]; reveal.target=self.app;
    NSMenuItem *archive = [menu addItemWithTitle:@"Note explorer" action:@selector(browse:) keyEquivalent:@""]; archive.target = self.app;
    return menu;
}
@end
