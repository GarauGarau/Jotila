#import "../../src/macos/App.h"
#include <stdio.h>
#import "../../src/macos/Palette.h"
#import "../../src/macos/Calendar.h"
#include <sys/stat.h>

@interface NSView (PaletteTests)
- (NSInteger)hit:(NSPoint)point;
@end
@interface JNApp (Tests)
- (void)installMenu;
- (void)screenChanged:(NSNotification *)notification;
- (void)restore;
@end
@interface JNBrowser (Tests)
- (void)toggleCalendar:(id)sender;
- (void)chooseDay:(NSUInteger)day rect:(NSRect)rect;
- (void)closeDay;
- (void)sidebarMode:(id)sender;
- (void)trashSelected:(id)sender;
- (BOOL)windowShouldClose:(NSWindow *)window;
@end
static unsigned checks;
static void check(BOOL ok, const char *label) {
    ++checks; if (!ok) { fprintf(stderr,"FAIL UI: %s\n",label); exit(1); }
}
static void pump(NSTimeInterval seconds) {
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:seconds];
    while (end.timeIntervalSinceNow>0) {
        NSEvent *event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01] inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
}
static void capture(NSView *view, NSString *name) {
    [view layoutSubtreeIfNeeded]; [view displayIfNeeded];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGImageRef image=CGWindowListCreateImage(CGRectNull,kCGWindowListOptionIncludingWindow,(CGWindowID)view.window.windowNumber,kCGWindowImageBoundsIgnoreFraming);
#pragma clang diagnostic pop
    NSBitmapImageRep *bitmap;
    if(image) { bitmap=[[NSBitmapImageRep alloc] initWithCGImage:image]; CGImageRelease(image); }
    else { bitmap=[view bitmapImageRepForCachingDisplayInRect:view.bounds]; [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap]; }
    NSString *directory=@"build/macos/ui-snapshots"; [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:NULL];
    check([[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[directory stringByAppendingPathComponent:name] atomically:YES],"capture real AppKit rendering");
}
static NSMenuItem *command(NSInteger tag) { NSMenuItem *item=[NSMenuItem new]; item.tag=tag; return item; }
static void setText(JNNote *note, NSString *text) {
    [note.editor insertText:text replacementRange:NSMakeRange(0,note.editor.string.length)];
    [note.editor.undoManager removeAllActions];
}
static void undo(JNNote *note) { check(note.editor.undoManager.canUndo,"command registers undo"); [note.editor.undoManager undo]; }
static void redo(JNNote *note) { check(note.editor.undoManager.canRedo,"command registers redo"); [note.editor.undoManager redo]; }
static NSEvent *key(NSWindow *window, NSString *characters, unsigned short code, NSEventModifierFlags modifiers) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:modifiers timestamp:0 windowNumber:window.windowNumber context:nil characters:characters charactersIgnoringModifiers:characters isARepeat:NO keyCode:code];
}
static void rightClickPalettes(JNNote *note) {
    NSRect original=note.window.frame;
    for(NSScreen *screen in NSScreen.screens) {
        NSRect frame=original; frame.origin=NSMakePoint(NSMinX(screen.visibleFrame)+32,NSMinY(screen.visibleFrame)+32);
        [note.window setFrame:JNConstrainFrame(frame,screen.visibleFrame) display:YES];
        for(NSUInteger i=0;i<12;++i) {
            [note.window makeKeyAndOrderFront:nil]; [note.window makeFirstResponder:note.editor];
            NSRange selection=NSMakeRange(0,i%2 ? MIN(5,note.editor.string.length) : 0);
            note.editor.selectedRange=selection;
            NSPoint point=[note.editor convertPoint:NSMakePoint(40,20) toView:nil];
            NSEvent *event=[NSEvent mouseEventWithType:NSEventTypeRightMouseDown location:point modifierFlags:0 timestamp:0 windowNumber:note.window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
            [note.window sendEvent:event];
            JNPalette *picker=[note valueForKey:@"picker"];
            check(picker.window.visible && picker.window.parentWindow==note.window,"right-click opens an attached palette");
            check(picker.window.screen==screen && NSContainsRect(screen.visibleFrame,picker.window.frame),"right-click palette stays on its screen");
            check(NSEqualRanges(note.editor.selectedRange,selection),"right-click preserves text selection");
            check([((NSTextField *)[picker valueForKey:@"mode"]).stringValue isEqual:selection.length ? @"Selected text" : @"Note color"],"right-click selects background or text mode");
            NSInteger background=[note.record.settings[@"palette"] integerValue];
            if(i%3) {
                [picker step:1]; [picker choose:0];
                check([note.record.settings[@"palette"] integerValue]==(selection.length ? background : picker.palette),"right-click applies only the requested color mode");
            } else [picker cancel:nil];
            check(!picker.window.visible && !picker.window.parentWindow,"right-click palette closes cleanly for repeated use");
        }
    }
    [note.window setFrame:original display:YES]; note.editor.selectedRange=NSMakeRange(0,0);
}
static NSView *findView(NSView *root, Class cls) {
    if([root isKindOfClass:cls]) return root;
    for(NSView *child in root.subviews) { NSView *found=findView(child,cls); if(found) return found; } return nil;
}
static void answerAlert(NSString *buttonTitle, void (^edit)(NSWindow *)) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,50*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
        NSWindow *window=NSApp.modalWindow; check(window!=nil,"native dialog is modal"); if(edit) edit(window);
        NSMutableArray *pending=[NSMutableArray arrayWithObject:window.contentView];
        while(pending.count) { NSView *v=pending.lastObject; [pending removeLastObject]; if([v isKindOfClass:NSButton.class] && [((NSButton *)v).title isEqual:buttonTitle]) { [(NSButton *)v performClick:nil]; return; } [pending addObjectsFromArray:v.subviews]; }
        check(NO,"dialog button exists");
    });
}
static void denseBrowserChecks(void) {
    NSURL *root=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    [NSFileManager.defaultManager createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:NULL];
    NSMutableString *ini=[NSMutableString string];
    for(NSUInteger i=0;i<300;++i) {
        NSString *identifier=[NSString stringWithFormat:@"20260101_grid_%03lu",(unsigned long)i];
        check([@"A realistic note preview\nWith another line." writeToURL:[root URLByAppendingPathComponent:[identifier stringByAppendingPathExtension:@"txt"]] atomically:NO encoding:NSUTF8StringEncoding error:NULL],"write private dense-grid fixture");
        [ini appendFormat:@"[%@]\nopen=0\ntags=work\n",identifier];
    }
    [ini writeToURL:[root URLByAppendingPathComponent:@"notes.ini"] atomically:NO encoding:NSUTF8StringEncoding error:NULL];
    NSError *error=nil; JNApp *app=[JNApp new]; app.store=[[JNStore alloc] initWithDirectory:root error:&error];
    check(app.store.records.count==300,"dense browser loads 300 notes"); [app browse:nil]; pump(0.1);
    JNBrowser *browser=[app valueForKey:@"browser"]; NSCollectionView *cards=[browser valueForKey:@"cards"];
    const CGFloat widths[]={660,850,1200,710,850};
    for(NSUInteger w=0;w<sizeof(widths)/sizeof(widths[0]);++w) {
        [browser.window setContentSize:NSMakeSize(widths[w],520)]; pump(0.05);
        NSRect frames[300]; BOOL separated=YES,valid=YES;
        for(NSUInteger i=0;i<300;++i) {
            NSCollectionViewLayoutAttributes *attributes=[cards.collectionViewLayout layoutAttributesForItemAtIndexPath:[NSIndexPath indexPathForItem:i inSection:0]];
            if(!attributes) { valid=NO; break; }
            frames[i]=attributes.frame;
            valid=valid && frames[i].size.width>0 && frames[i].size.height>0 && NSMinX(frames[i])>=0 && NSMaxX(frames[i])<=cards.bounds.size.width+1;
            for(NSUInteger j=0;j<i;++j) {
                if(NSIntersectsRect(frames[i],frames[j])) separated=NO;
                if(fabs(NSMinY(frames[i])-NSMinY(frames[j]))<1 && fabs(NSMinX(frames[i])-NSMinX(frames[j]))-frames[i].size.width<3.9) separated=NO;
                if(fabs(NSMinX(frames[i])-NSMinX(frames[j]))<1 && fabs(NSMinY(frames[i])-NSMinY(frames[j]))-frames[i].size.height<3.9) separated=NO;
            }
        }
        check(valid && separated,"dense native grid keeps all rows and columns separated after resize");
    }
    [browser toggleCalendar:nil]; [browser toggleCalendar:nil]; pump(0.05);
    check([cards numberOfItemsInSection:0]==300 && cards.visibleItems.count>0,"dense grid returns from calendar with visible cards");
    [browser windowShouldClose:browser.window]; [NSFileManager.defaultManager removeItemAtURL:root error:NULL];
}
static void hiddenScrollChecks(JNNote *note) {
    NSScrollView *scroll=note.editor.enclosingScrollView;
    check(!scroll.hasVerticalScroller && !scroll.hasHorizontalScroller,"note never displays a scrollbar");
    check(fabs(scroll.contentSize.width-scroll.bounds.size.width)<0.5,"hidden scrollbar reserves no gutter");
}
static void scrollingAndRecallChecks(void) {
    NSURL *root=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSError *error=nil; JNApp *app=[JNApp new]; app.store=[[JNStore alloc] initWithDirectory:root error:&error];
    check(app.store!=nil,"isolated scrolling archive");
    [app newNote:nil]; JNNote *note=app.notes.allValues.firstObject;
    [note.editor insertText:@"Short note" replacementRange:NSMakeRange(0,0)];
    check([note flush:&error] && !note.scrolling,"legacy default remains bounded");
    NSString *identifier=note.record.identifier;
    NSMutableString *longText=[NSMutableString string];
    for (NSUInteger i=0;i<160;++i) [longText appendFormat:@"Line %lu — Caffè 🌿\n",(unsigned long)i];
    NSPasteboard *clipboard=[NSPasteboard pasteboardWithUniqueName];
    [clipboard setString:longText forType:NSPasteboardTypeString];
    note.editor.selectedRange=NSMakeRange(0,note.editor.string.length);
    answerAlert(@"OK",nil); [note.editor readSelectionFromPasteboard:clipboard type:NSPasteboardTypeString];
    check([note.editor.string isEqual:@"Short note"],"rejected paste explains overflow and preserves text");
    answerAlert(@"Cancel",nil); [note toggleScrolling:nil]; check(!note.scrolling,"scrolling scope can be cancelled");
    answerAlert(@"Only this note",nil); [note toggleScrolling:nil];
    check(note.scrolling && ![app.store.preferences[@"scroll"] boolValue],"per-note scrolling leaves default unchanged");
    [app newNote:nil]; JNNote *other=nil;
    for(JNNote *candidate in app.notes.allValues) if(candidate!=note) other=candidate;
    check(other && !other.scrolling,"other notes retain bounded mode");
    answerAlert(@"Only this note",nil); [note toggleScrolling:nil];
    check(!note.scrolling,"short text can return to bounded mode");
    answerAlert(@"This note and new notes",nil); [note toggleScrolling:nil];
    check(note.scrolling && [app.store.preferences[@"scroll"] boolValue] && !other.scrolling,"saved default does not alter existing notes");
    note.editor.selectedRange=NSMakeRange(0,note.editor.string.length);
    [note.editor readSelectionFromPasteboard:clipboard type:NSPasteboardTypeString];
    check([note.editor.string isEqual:longText],"scrollable notes accept complete long paste");
    check([note resizeTo:NSMakeSize(240,180)],"scrollable note can shrink without clipping its document");
    hiddenScrollChecks(note);
    [note.editor moveToBeginningOfDocument:nil]; [note.editor scrollRangeToVisible:note.editor.selectedRange];
    NSScrollView *scroll=note.editor.enclosingScrollView;
    CGFloat top=scroll.contentView.bounds.origin.y;
    CGEventRef wheel=CGEventCreateScrollWheelEvent(NULL,kCGScrollEventUnitLine,1,-5);
    [note.editor scrollWheel:[NSEvent eventWithCGEvent:wheel]]; CFRelease(wheel); pump(0.1);
    check(scroll.contentView.bounds.origin.y>top,"native wheel scrolls with no visible scrollbar");
    [note.editor moveToEndOfDocument:nil]; [note.editor scrollRangeToVisible:note.editor.selectedRange];
    check(scroll.contentView.bounds.origin.y>top,"native end navigation reveals the end");
    [note.editor moveToBeginningOfDocument:nil]; [note.editor scrollRangeToVisible:note.editor.selectedRange];
    check(scroll.contentView.bounds.origin.y<1,"native home navigation returns to the top");
    [note.editor pageDown:nil]; check(scroll.contentView.bounds.origin.y>top,"Page Down scrolls the native document");
    [note.editor pageUp:nil]; hiddenScrollChecks(note);
    answerAlert(@"OK",nil); [note toggleScrolling:nil]; check(note.scrolling,"overflow blocks disabling scrolling without losing text");
    [note fold:nil]; [note fold:nil]; hiddenScrollChecks(note);
    [note showAtPointer:NO activate:YES];
    check([app flush:&error],"save recent identity, long note and scrolling default");
    [note close]; pump(0.1); check(!app.notes[identifier],"close recent note into archive");
    app.store.preferences[@"last_note"]=identifier;
    [app recallNote:nil]; note=app.notes[identifier];
    check(note && note.window.visible && note.scrolling && [note.editor.string isEqual:longText],"recall reopens same archived long note");
    [other detach]; [app.notes removeObjectForKey:other.record.identifier];
    [note showAtPointer:NO activate:YES]; check([app flush:&error],"persist recent note before reload");
    [note detach]; [app.notes removeAllObjects];
    check([app.store reload:&error],"read settings back from disk");
    [app restore]; note=app.notes[identifier];
    check(note.scrolling && [app.store.preferences[@"scroll"] boolValue] && [app.store.preferences[@"last_note"] isEqual:identifier],
          "restoration preserves per-note mode, default and last-note identity");
    hiddenScrollChecks(note);
    [app newNote:nil]; JNNote *fresh=nil;
    for(JNNote *candidate in app.notes.allValues) if(candidate!=note) fresh=candidate;
    check(fresh.scrolling,"new note inherits reloaded default");
    [note showAtPointer:NO activate:YES]; [note fold:nil]; [note.window orderOut:nil];
    NSUInteger count=app.notes.count; [app installMenu];
    NSStatusItem *status=[app valueForKey:@"status"]; [status.button performClick:nil];
    check(!note.window.visible,"single status click waits for double-click interval");
    pump(NSEvent.doubleClickInterval+0.1);
    check(note.window.visible && !note.collapsed && app.notes.count==count,"single status click recalls and unfolds without creating a note");
    [note.window orderOut:nil]; pump(NSEvent.doubleClickInterval+0.1);
    check(!note.window.visible,"status click timer does not repeat");
    [note detach]; [app.notes removeObjectForKey:identifier];
    check([app.store remove:note.record error:&error],"delete recent note fixture");
    app.store.preferences[@"last_note"]=identifier; [app recallNote:nil];
    JNBrowser *browser=[app valueForKey:@"browser"];
    check(browser.window.visible && !app.notes[identifier],"missing recent note opens explorer without recreating it");
    for(JNNote *n in app.notes.allValues) [n detach];
    [browser.window orderOut:nil]; [clipboard releaseGlobally];
    [NSFileManager.defaultManager removeItemAtURL:root error:NULL];
}
static void profile(void) {
    NSURL *root=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    [NSFileManager.defaultManager createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:NULL];
    NSMutableString *ini=[NSMutableString string];
    for(NSUInteger i=0;i<10000;++i) { @autoreleasepool {
        NSString *identifier=[NSString stringWithFormat:@"20260101_%05lu",(unsigned long)i];
        NSString *body=[NSString stringWithFormat:@"#Note %lu\r\n%@ — café 🌿\r\n",(unsigned long)i,i%1000==0?@"Needle":@"Ordinary note"];
        [body writeToURL:[root URLByAppendingPathComponent:[identifier stringByAppendingPathExtension:@"txt"]] atomically:NO encoding:NSUTF8StringEncoding error:NULL];
        [ini appendFormat:@"[%@]\nopen=0\nhas_title=1\ntags=%@\npalette=0\ncolor=%lu\n",identifier,i%5==0?@"work":@"personal",(unsigned long)(i%6)];
    } }
    [ini writeToURL:[root URLByAppendingPathComponent:@"notes.ini"] atomically:NO encoding:NSUTF8StringEncoding error:NULL];
    CFAbsoluteTime started=CFAbsoluteTimeGetCurrent(); NSError *error=nil; JNApp *app=[JNApp new]; app.store=[[JNStore alloc] initWithDirectory:root error:&error];
    check(app.store.records.count==10000,"benchmark loads 10,000 metadata records"); double load=(CFAbsoluteTimeGetCurrent()-started)*1000;
    started=CFAbsoluteTimeGetCurrent(); [app browse:nil]; pump(0.25); JNBrowser *browser=[app valueForKey:@"browser"];
    NSUInteger previews=[[browser valueForKey:@"previewLoads"] unsignedIntegerValue]; NSCollectionView *cards=[browser valueForKey:@"cards"];
    check([[browser valueForKey:@"results"] count]==10000,"benchmark explorer includes all records");
    check(previews<50 && cards.visibleItems.count<50,"benchmark preview work is bounded by visible cards"); double first=(CFAbsoluteTimeGetCurrent()-started)*1000;
    NSSearchField *search=[browser valueForKey:@"search"]; search.stringValue=@"#work"; started=CFAbsoluteTimeGetCurrent(); [browser refresh];
    check([[browser valueForKey:@"results"] count]==2000,"benchmark tag-only search uses metadata"); double tags=(CFAbsoluteTimeGetCurrent()-started)*1000;
    search.stringValue=@"Needle"; started=CFAbsoluteTimeGetCurrent(); [browser refresh];
    NSOperationQueue *queue=[browser valueForKey:@"queue"]; NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:30]; while(queue.operationCount && deadline.timeIntervalSinceNow>0) pump(0.02); pump(0.05);
    check([[browser valueForKey:@"results"] count]==10,"benchmark streaming search finds sparse matches");
    double full=(CFAbsoluteTimeGetCurrent()-started)*1000;
    search.stringValue=@""; [browser refresh]; JNRecord *record=app.store.records.firstObject; [app openRecord:record activate:NO]; JNNote *note=app.notes[record.identifier];
    [note.editor insertText:@" first edit" replacementRange:NSMakeRange(note.editor.string.length,0)]; check([note flush:&error],"benchmark initializes native geometry"); pump(0.05);
    NSUInteger generation=[[browser valueForKey:@"generation"] unsignedIntegerValue]; NSURL *metadata=[root URLByAppendingPathComponent:@"notes.ini"]; struct stat before,after; stat(metadata.fileSystemRepresentation,&before);
    started=CFAbsoluteTimeGetCurrent(); [note.editor insertText:@" second edit" replacementRange:NSMakeRange(note.editor.string.length,0)]; check([note flush:&error],"benchmark saves a text edit"); double edit=(CFAbsoluteTimeGetCurrent()-started)*1000; stat(metadata.fileSystemRepresentation,&after);
    check(before.st_ino==after.st_ino && before.st_mtimespec.tv_nsec==after.st_mtimespec.tv_nsec,"body edit does not rewrite metadata");
    check(generation==[[browser valueForKey:@"generation"] unsignedIntegerValue],"body edit updates one card without rebuilding search snapshot"); [note detach];
    printf("PROFILE body edit with 10000 notes: %.1f ms; no metadata replacement or explorer rebuild.\n",edit);
    printf("PROFILE 10000 notes: metadata %.1f ms; first cards %.1f ms (includes 250 ms event pump); %lu preview loads; tag filter %.1f ms; full-text search %.1f ms.\n",load,first,(unsigned long)previews,tags,full);
    [browser windowShouldClose:browser.window]; [NSFileManager.defaultManager removeItemAtURL:root error:NULL];
}
int main(void) { @autoreleasepool {
    [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; [NSApp finishLaunching];
    /* The command-line test runner has no Finder launch to hand it focus.
       Activation is asynchronous, so establish it before checking key windows. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    NSDate *activationDeadline=[NSDate dateWithTimeIntervalSinceNow:5];
    while(!NSApp.active && activationDeadline.timeIntervalSinceNow>0) pump(0.02);
    check(NSApp.active,"UI test process acquires application focus");
    NSURL *root=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES] URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSError *error=nil; JNApp *app=[JNApp new]; app.store=[[JNStore alloc] initWithDirectory:root error:&error];
    check(app.store!=nil,"isolated UI archive");
    check(JNWrite([NSData data],[root URLByAppendingPathComponent:@"legacy-empty.txt"],&error),"legacy empty note fixture");
    check([app.store reload:&error],"reload archive containing legacy empty note");
    [app newNote:nil];
    JNNote *note=app.notes.allValues.firstObject;
    [app browse:nil];
    check([[[app valueForKey:@"browser"] valueForKey:@"results"] count]==0,"fresh and legacy empty notes do not appear in explorer");
    answerAlert(@"Save",^(NSWindow *w){ ((NSTextField *)w.initialFirstResponder).stringValue=@"Title only"; }); [note rename:nil];
    check([note flush:&error] && app.store.records.count==1,"title alone is enough to save a note");
    check([[[app valueForKey:@"browser"] valueForKey:@"results"] count]==1,"title-only note appears in explorer");
    answerAlert(@"Save",^(NSWindow *w){ ((NSTextField *)w.initialFirstResponder).stringValue=@"  \t"; }); [note rename:nil];
    check([note flush:&error] && app.store.records.count==0,"cleared title with empty body removes archive entry");
    check([[[app valueForKey:@"browser"] valueForKey:@"results"] count]==0,"cleared note disappears immediately from explorer");
    answerAlert(@"Save",^(NSWindow *w){ ((NSTextField *)w.initialFirstResponder).stringValue=@""; }); [note rename:nil];
    [note showAtPointer:NO activate:YES];
    [note.editor insertText:@"First native note\nCaffè 🌿" replacementRange:NSMakeRange(0,0)];
    check([note flush:&error],"editor saves typed text"); check(app.store.records.count==1,"typed note enters archive");
    [app browse:nil]; pump(0.5);
    JNBrowser *browser=[app valueForKey:@"browser"];
    NSButton *calendarButton=[browser valueForKey:@"calendarButton"];
    check(calendarButton.image!=nil && calendarButton.title.length==0 && calendarButton.imagePosition==NSImageOnly,"browser calendar uses a native image instead of a font glyph");
    [calendarButton performClick:nil]; check([[browser valueForKey:@"calendarVisible"] boolValue] && calendarButton.state==NSControlStateValueOn,"calendar icon opens year view and shows selection");
    [calendarButton performClick:nil]; check(![[browser valueForKey:@"calendarVisible"] boolValue] && calendarButton.state==NSControlStateValueOff,"calendar icon returns to notes");
    pump(0.1);
    NSArray *results=[browser valueForKey:@"results"];
    check(browser.window.visible,"explorer visible"); check(results.count==1,"explorer shows saved note on first open");
    NSCollectionView *cards=[browser valueForKey:@"cards"];
    check([cards numberOfItemsInSection:0]==1 && cards.visibleItems.count==1,"explorer renders a visible note card");
    capture(browser.window.contentView,@"archive.png"); capture(note.window.contentView,@"note.png");
    [app installMenu]; [note.window makeKeyAndOrderFront:nil]; [note.window makeFirstResponder:note.editor]; pump(0.05);
    NSStatusItem *status=[app valueForKey:@"status"];
    check(status.visible && status.button.image.isTemplate && NSEqualSizes(status.button.image.size,NSMakeSize(18,18)),"menu-bar icon is a visible Retina-scalable template image");
    check(status.button.title.length==0 && status.length==NSSquareStatusItemLength && status.button.imagePosition==NSImageOnly,"menu-bar item reserves a square icon slot");
    check(status.button.image.TIFFRepresentation.length>0,"menu-bar vector icon renders");
    capture(status.button.window.contentView,@"menu-bar.png");
    check(note.window.keyWindow && note.window.firstResponder==note.editor,"note accepts actual text focus");
    rightClickPalettes(note);
    check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"e",14,NSEventModifierFlagCommand)],"Command E dispatches explorer");
    check(browser.window.keyWindow,"Command E focuses explorer");
    [note.window makeKeyAndOrderFront:nil]; [note.window makeFirstResponder:note.editor];
    check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"f",3,NSEventModifierFlagCommand)],"Command F dispatches search");
    check([browser.window.firstResponder isKindOfClass:NSTextView.class],"search receives text focus");
    [note showAtPointer:NO activate:YES];
    for(NSInteger style=1;style<=4;++style) {
        note.editor.selectedRange=NSMakeRange(0,5); [note.editor.undoManager removeAllActions];
        NSAttributedString *before=[note.editor.textStorage copy]; [note format:command(style)];
        check(![before isEqualToAttributedString:note.editor.textStorage],"format changes selected attributes");
        undo(note); check([before isEqualToAttributedString:note.editor.textStorage],"format undo restores mixed attributes"); redo(note);
    }
    note.editor.selectedRange=NSMakeRange(0,5); CGFloat beforeFont=[[note.editor.textStorage attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL] pointSize];
    [note.editor.undoManager removeAllActions]; [note resizeBy:1]; check([[note.editor.textStorage attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL] pointSize]>beforeFont,"selection font size increases"); undo(note);
    check([[note.editor.textStorage attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL] pointSize]==beforeFont,"font size undo");
    note.editor.selectedRange=NSMakeRange(0,0); NSSize oldSize=note.window.frame.size; [note resizeBy:1]; check(note.window.frame.size.width>oldSize.width,"unselected note resizes");
    [note fold:nil]; check(note.window.frame.size.height==32,"folded height"); [note resizeBy:1]; [note fold:nil]; check(note.window.frame.size.height>oldSize.height,"folded resize preserves expanded dimensions");
    check(![note resizeTo:NSMakeSize(100,50)],"minimum note size enforced");
    NSString *accepted=[note.editor.string copy]; NSMutableString *overflow=[NSMutableString string]; for(NSInteger i=0;i<100;++i) [overflow appendString:@"overflow\n"];
    [note.editor insertText:overflow replacementRange:NSMakeRange(note.editor.string.length,0)]; check([note.editor.string isEqual:accepted],"typing or paste cannot clip existing content");
    NSPasteboard *clipboard=[NSPasteboard pasteboardWithUniqueName]; note.editor.selectedRange=NSMakeRange(0,5);
    check([note.editor writeSelectionToPasteboard:clipboard types:note.editor.writablePasteboardTypes],"native copy exports selected Unicode text");
    check([[clipboard stringForType:NSPasteboardTypeString] isEqual:@"First"],"copied selection is exact");
    note.editor.selectedRange=NSMakeRange(note.editor.string.length,0); check([note.editor readSelectionFromPasteboard:clipboard type:NSPasteboardTypeString],"native paste reads plain text");
    [clipboard releaseGlobally];
    for(NSInteger opacity=60;opacity<=100;opacity+=10) { check([NSApp sendAction:NSSelectorFromString(@"opacity:") to:note from:command(opacity)],"opacity menu command dispatches"); check(fabs(note.window.alphaValue-opacity/100.0)<0.001,"opacity applied"); }

    check([note resizeTo:NSMakeSize(420,340)],"large note preset fits");
    NSRect detached=note.window.frame; detached.origin=NSMakePoint(-9000,5000); [note.window setFrame:detached display:NO]; [app screenChanged:nil];
    check(NSContainsRect(NSScreen.mainScreen.visibleFrame,note.window.frame),"disconnected display recovery keeps entire note reachable");
    NSRect secondary=NSMakeRect(-1920,0,1920,1080); check(NSContainsRect(secondary,JNConstrainFrame(NSMakeRect(-2500,900,900,500),secondary)),"window constraints support negative display coordinates");
    note.editor.selectedRange=NSMakeRange(0,5); CGFloat selectedFont=[[note.editor.textStorage attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL] pointSize]; [note fold:nil]; [note resizeBy:1]; [note fold:nil];
    check([[note.editor.textStorage attribute:NSFontAttributeName atIndex:0 effectiveRange:NULL] pointSize]==selectedFont,"collapsed zoom changes note dimensions despite hidden selection");
    note.editor.selectedRange=NSMakeRange(0,0);

    setText(note,@"* on an ordinary line"); check(![note insertNewlineWithShift:YES],"only an isolated asterisk creates checklist");
    setText(note,@"*"); note.editor.selectedRange=NSMakeRange(1,0); check([note insertNewlineWithShift:YES] && [note.editor.string isEqual:@"☐ "],"Shift Enter starts checklist");
    [note.editor insertText:@"Task" replacementRange:note.editor.selectedRange];
    [note.editor.undoManager removeAllActions]; [note toggleChecklistAt:0];
    check([note.editor.string isEqual:@"☑ Task"] && [[note.editor.textStorage attribute:NSStrikethroughStyleAttributeName atIndex:2 effectiveRange:NULL] integerValue]!=0,"checkbox toggles and strikes whole task");
    undo(note); check([note.editor.string isEqual:@"☐ Task"],"checkbox undo restores text"); redo(note);
    note.editor.selectedRange=NSMakeRange(note.editor.string.length,0); check([note insertNewlineWithShift:NO] && [note.editor.string hasSuffix:@"\n☐ "],"Enter continues checklist unchecked");
    check([[note.editor.typingAttributes objectForKey:NSStrikethroughStyleAttributeName] integerValue]==0,"new item does not inherit strike");
    check([note insertNewlineWithShift:NO] && [note.editor.string hasSuffix:@"\n"],"Enter on empty checklist exits");
    setText(note,@"☐ Item"); note.editor.selectedRange=NSMakeRange(2,0); [note.editor doCommandBySelector:@selector(deleteBackward:)]; check([note.editor.string isEqual:@"Item"],"Backspace removes checklist marker");
    setText(note,@"#Project\nBody"); note.editor.selectedRange=NSMakeRange(8,0); NSString *oldTitle=note.record.title;
    check([note insertNewlineWithShift:YES] && [note.record.title isEqual:@"Project"] && [note.editor.string isEqual:@"Body"],"title promotion removes first line");
    undo(note); check([note.record.title isEqual:oldTitle] && [note.editor.string isEqual:@"#Project\nBody"],"title promotion undo is atomic"); redo(note);
    answerAlert(@"Save",^(NSWindow *w){ NSTextField *field=(NSTextField *)findView(w.contentView,NSTextField.class); (void)field; NSTextField *input=(NSTextField *)w.initialFirstResponder; input.stringValue=@"Renamed"; });
    [note rename:nil]; check([note.record.title isEqual:@"Renamed"],"rename dialog saves title");
    answerAlert(@"Cancel",nil); [note rename:nil]; check([note.record.title isEqual:@"Renamed"],"rename cancellation");
    answerAlert(@"Save",^(NSWindow *w){ NSTokenField *field=(NSTokenField *)findView(w.contentView,NSTokenField.class); field.objectValue=@[@"Work",@"café"]; });
    [note showAtPointer:NO activate:YES]; check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"t",17,NSEventModifierFlagCommand)],"Command T opens tags"); check([note.record.tags isEqual:@"café;work"],"tag dialog normalizes tokens");
    note.editor.selectedRange=NSMakeRange(0,0); JNPalette *picker=[[JNPalette alloc] initWithNote:note]; [picker showAtPoint:NSMakePoint(500,500)]; pump(0.15); capture(picker.window.contentView,@"palette.png");
    NSInteger initial=picker.palette; [picker step:1]; [picker step:-1]; check(picker.palette==initial,"palette left/right navigation");
    for(NSInteger p=0;p<POPULAR_PALETTE_COUNT;++p) { NSInteger palette=picker.palette; check(palette>=3 && palette<PALETTE_COUNT,"all popular palettes reachable");
        NSView *wheel=[picker valueForKey:@"wheel"];
        for(NSInteger c=0;c<PALETTE_SIZES[palette];++c) { CGFloat angle=(90-(c+0.5)*360/PALETTE_SIZES[palette])*M_PI/180; check([wheel hit:NSMakePoint(110+50*cos(angle),110+50*sin(angle))]==c,"every color wedge hit matches its rendering"); }
        check(wheel.accessibilityChildren.count==PALETTE_SIZES[palette],"all palette colors exposed to accessibility"); [picker step:1]; }
    check(picker.palette==initial,"palette navigation wraps"); [picker choose:2];
    check([note.record.settings[@"palette"] integerValue]==initial && [note.record.settings[@"color"] integerValue]==2 && app.sessionPalette==initial,"background palette applies to note and session");
    picker=[[JNPalette alloc] initWithNote:note]; [picker toggleFavorite:nil]; check([app.store.preferences[@"favorite_palette"] integerValue]==picker.palette,"favorite persists");
    [picker toggleFavorite:nil]; check([app.store.preferences[@"favorite_palette"] integerValue]==-1,"favorite can be cleared");
    note.editor.selectedRange=NSMakeRange(0,MIN(2,note.editor.string.length)); NSInteger background=[note.record.settings[@"palette"] integerValue];
    picker=[[JNPalette alloc] initWithNote:note]; [picker step:1]; [picker choose:1]; check([note.record.settings[@"palette"] integerValue]==background,"selected-text color preserves background");
    check([note flush:&error],"rich edits save"); struct stat txtBefore,txtAfter; NSURL *txt=[app.store URLFor:note.record extension:@"txt"]; stat(txt.fileSystemRepresentation,&txtBefore);
    note.editor.selectedRange=NSMakeRange(0,1); [note format:command(3)]; check([note flush:&error],"format-only save"); stat(txt.fileSystemRepresentation,&txtAfter); check(txtBefore.st_ino==txtAfter.st_ino,"format-only save leaves TXT unchanged");
    answerAlert(@"Save",^(NSWindow *w){ NSDatePicker *date=(NSDatePicker *)findView(w.contentView,NSDatePicker.class); date.dateValue=[[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian] dateFromComponents:({ NSDateComponents *c=[NSDateComponents new]; c.year=2028;c.month=2;c.day=29;c; })]; });
    [note showAtPointer:NO activate:YES]; check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"d",2,NSEventModifierFlagCommand)],"Command D opens date picker"); check(note.record.day==20280229,"day picker assigns leap day"); check([note flush:&error],"calendar date saves");
    [app browse:nil]; [browser toggleCalendar:nil]; pump(0.1); JNCalendar *calendar=[browser valueForKey:@"calendar"]; calendar.year=2028; check(calendar.accessibilityChildren.count==366,"calendar exposes every leap-year day to accessibility"); capture(browser.window.contentView,@"calendar.png");
    for(NSInteger day=20280101;day<=20281231;day=date_shift((unsigned)day,1)) check(NSContainsRect(calendar.bounds,[calendar cell:(unsigned)day]),"every calendar day stays in its view");
    calendar.selected=20280228; [calendar moveSelection:1]; check(calendar.selected==20280229,"calendar keyboard navigation handles leap day"); [calendar moveSelection:1]; check(calendar.selected==20280301,"calendar crosses month");
    for(NSInteger width=660;width<=1200;width+=180) { [browser.window setContentSize:NSMakeSize(width,480)]; pump(0.02); NSScrollView *scroll=[browser valueForKey:@"calendarScroll"]; check(NSContainsRect(browser.window.contentView.bounds,scroll.frame),"calendar fits resized browser"); }
    [browser toggleCalendar:nil]; [browser.window setContentSize:NSMakeSize(850,640)];
    NSSearchField *search=[browser valueForKey:@"search"]; search.stringValue=@"missing-term"; [browser refresh]; pump(0.25); check([[browser valueForKey:@"results"] count]==0,"full-text search excludes missing text");
    search.stringValue=@"Body"; [browser refresh]; pump(0.25); check([[browser valueForKey:@"results"] count]==1,"full-text search returns saved note");
    search.stringValue=@"#work"; [browser refresh]; check([[browser valueForKey:@"results"] count]==1,"tag-only search resolves synchronously");
    search.stringValue=@"-#"; [browser refresh]; check([[browser valueForKey:@"results"] count]==0,"untagged filter"); search.stringValue=@""; [browser refresh];
    [browser.window performClose:nil]; [app browse:nil]; check([[browser valueForKey:@"results"] count]==1,"explorer reopens with notes");
    [note.window makeKeyAndOrderFront:nil]; [note.window makeFirstResponder:note.editor];
    NSUInteger count=app.notes.count; check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"n",45,NSEventModifierFlagCommand)] && app.notes.count==count+1,"Command N creates note");
    JNNote *second=nil; for(JNNote *n in app.notes.allValues) if(n!=note) second=n;
    check(second!=nil,"new note controller exists");
    setText(second,@"Second note"); second.record.day=20280229; check([second flush:&error],"second note saves with same day");
    [app browse:nil]; [browser toggleCalendar:nil]; [browser chooseDay:20280229 rect:[calendar cell:20280229]]; pump(0.05);
    NSPopover *dayPopover=[browser valueForKey:@"dayPopover"]; NSTableView *dayTable=[browser valueForKey:@"dayTable"];
    check(dayPopover.shown && dayTable.numberOfRows==2,"day with multiple notes opens a title list");
    [dayTable keyDown:key(dayTable.window,@"",119,0)]; check(dayTable.selectedRow==1,"day list End selects last note");
    [dayTable keyDown:key(dayTable.window,@"",115,0)]; check(dayTable.selectedRow==0,"day list Home selects first note");
    [dayTable keyDown:key(dayTable.window,@"",121,0)]; check(dayTable.selectedRow==1,"day list Page Down");
    [dayTable keyDown:key(dayTable.window,@"",116,0)]; check(dayTable.selectedRow==0,"day list Page Up");
    [dayTable keyDown:key(dayTable.window,@"",53,0)]; NSDate *closeDeadline=[NSDate dateWithTimeIntervalSinceNow:2]; while(dayPopover.shown && closeDeadline.timeIntervalSinceNow>0) pump(0.02); check(!dayPopover.shown,"day list Escape returns to calendar");
    [browser chooseDay:20280229 rect:[calendar cell:20280229]]; dayTable=[browser valueForKey:@"dayTable"];
    [dayTable keyDown:key(dayTable.window,@"\r",36,0)]; check(![[browser valueForKey:@"dayPopover"] isShown],"day list Enter opens note and closes list");
    [note showAtPointer:NO activate:YES]; note.editor.selectedRange=NSMakeRange(0,1);
    for(NSString *shortcut in @[@"b",@"i",@"u",@"X",@"-",@"+"]) check([NSApp.mainMenu performKeyEquivalent:key(note.window,shortcut,0,NSEventModifierFlagCommand|([shortcut isEqual:@"X"]?NSEventModifierFlagShift:0))],"format keyboard shortcut dispatches");
    check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"s",1,NSEventModifierFlagCommand)],"Command S saves all notes");
    NSString *saved=[note.editor.string copy]; JNRecord *record=note.record; [note close]; pump(0.05);
    check(!app.notes[record.identifier] && ![record.settings[@"open"] boolValue],"close archives note and removes controller");
    [app openRecord:record activate:YES]; note=app.notes[record.identifier]; check([note.editor.string isEqual:saved],"archived note reopens with content");
    [note.editor.undoManager removeAllActions]; note.editor.selectedRange=NSMakeRange(note.editor.string.length,0); NSFont *typingBefore=note.editor.typingAttributes[NSFontAttributeName]; [note format:command(2)];
    check(![note.editor.typingAttributes[NSFontAttributeName] isEqual:typingBefore],"format at caret updates typing style"); undo(note);
    [note.editor insertText:@" restored" replacementRange:note.editor.selectedRange]; check([note flush:&error],"typing after reopen saves");
    [note.editor.undoManager removeAllActions]; NSString *beforeClear=[note.editor.string copy]; [note.editor insertText:@"" replacementRange:NSMakeRange(0,note.editor.string.length)];
    // Remove the explicit title as well so the note is actually empty.
    answerAlert(@"Save",^(NSWindow *w){ ((NSTextField *)w.initialFirstResponder).stringValue=@""; }); [note rename:nil]; check([note flush:&error],"clear content and title");
    check(![app.store.records containsObject:note.record],"empty saved note disappears from archive");
    [note.editor insertText:beforeClear replacementRange:NSMakeRange(0,0)]; check([note flush:&error],"cleared note can be restored");
    [app browse:nil]; NSCollectionView *collection=[browser valueForKey:@"cards"];
    NSArray *current=[browser valueForKey:@"results"]; NSUInteger secondIndex=[current indexOfObjectPassingTest:^BOOL(NSDictionary *entry,NSUInteger i,BOOL *stop){(void)i;(void)stop;return [entry[@"id"] isEqual:second.record.identifier];}];
    collection.selectionIndexPaths=[NSSet setWithObject:[NSIndexPath indexPathForItem:secondIndex inSection:0]];
    answerAlert(@"Cancel",nil); [browser trashSelected:nil]; check([app.store.records containsObject:second.record],"delete cancellation retains note");
    answerAlert(@"Move to Trash",nil); [browser trashSelected:nil]; check(![app.store.records containsObject:second.record],"confirmed deletion removes selected note");
    [note showAtPointer:NO activate:YES]; check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"p",35,NSEventModifierFlagCommand)],"Command P opens color wheel");
    JNPalette *keyboardPicker=[note valueForKey:@"picker"]; NSView *keyboardWheel=[keyboardPicker valueForKey:@"wheel"]; NSInteger keyboardPalette=keyboardPicker.palette;
    [keyboardWheel keyDown:key(keyboardPicker.window,@"",124,0)]; check(keyboardPicker.palette!=keyboardPalette,"wheel Right key changes palette");
    [keyboardWheel keyDown:key(keyboardPicker.window,@"",123,0)]; check(keyboardPicker.palette==keyboardPalette,"wheel Left key restores palette");
    [keyboardWheel keyDown:key(keyboardPicker.window,@"",125,0)]; [keyboardWheel keyDown:key(keyboardPicker.window,@"\r",36,0)]; check(!keyboardPicker.window.visible,"wheel Enter applies and dismisses");
    [note showPalette:nil]; keyboardPicker=[note valueForKey:@"picker"]; keyboardWheel=[keyboardPicker valueForKey:@"wheel"];
    [keyboardWheel keyDown:key(keyboardPicker.window,@"f",3,0)]; check([app.store.preferences[@"favorite_palette"] integerValue]==keyboardPicker.palette,"wheel F saves favorite");
    [keyboardWheel keyDown:key(keyboardPicker.window,@"f",3,0)]; check([app.store.preferences[@"favorite_palette"] integerValue]==-1,"wheel F clears favorite");
    [keyboardWheel keyDown:key(keyboardPicker.window,@"",53,0)]; check(!keyboardPicker.window.visible,"wheel Escape dismisses");
    check([note flush:&error],"flush before external reload");
    check(JNWrite([@"External edit\n" dataUsingEncoding:NSUTF8StringEncoding],[app.store URLFor:note.record extension:@"txt"],&error),"simulate external edit for reload shortcut");
    NSString *reloadedID=note.record.identifier; [note showAtPointer:NO activate:YES];
    check([NSApp.mainMenu performKeyEquivalent:key(note.window,@"r",15,NSEventModifierFlagCommand)],"Command R reloads external edits");
    check([app.notes[reloadedID].editor.string isEqual:@"External edit\n"],"reload prefers changed TXT over stale RTF");
    for(JNNote *n in app.notes.allValues) { [n detach]; }
    [browser.window orderOut:nil];
    [NSFileManager.defaultManager removeItemAtURL:root error:NULL];
    denseBrowserChecks();
    scrollingAndRecallChecks();
    if(getenv("JOTILLA_PROFILE")) profile();
    printf("PASS: %u native UI checks.\n",checks);
} return 0; }
