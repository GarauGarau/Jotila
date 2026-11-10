#import "App.h"
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>

/* A template image stays legible in both menu-bar appearances and at Retina
   scale. Do not use a font glyph: its weight and advance vary by system font. */
static NSImage *statusIcon(void) {
    NSImage *image=[NSImage imageWithSize:NSMakeSize(18,18) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        (void)rect; [NSColor.blackColor setStroke];
        NSBezierPath *note=[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(1.5,1.5,15,15) xRadius:1 yRadius:1];
        note.lineWidth=1.5; [note stroke];
        NSBezierPath *bar=[NSBezierPath bezierPath]; bar.lineWidth=1.5;
        [bar moveToPoint:NSMakePoint(2,12.5)]; [bar lineToPoint:NSMakePoint(16,12.5)]; [bar stroke];
        NSBezierPath *iota=[NSBezierPath bezierPath]; iota.lineWidth=2; iota.lineCapStyle=NSLineCapStyleRound;
        [iota moveToPoint:NSMakePoint(7,9.5)]; [iota lineToPoint:NSMakePoint(9,10)]; [iota lineToPoint:NSMakePoint(9,5.5)];
        [iota curveToPoint:NSMakePoint(12,5.5) controlPoint1:NSMakePoint(9,3.5) controlPoint2:NSMakePoint(11.5,3.5)]; [iota stroke];
        return YES;
    }];
    image.template=YES; image.accessibilityDescription=@"Jotilla notes"; return image;
}

static OSStatus hotkey(EventHandlerCallRef handler, EventRef event, void *context) {
    (void)handler; (void)event;
    JNApp *app = (__bridge JNApp *)context;
    [app newNote:@"pointer"]; return noErr;
}
@implementation JNApp {
    NSStatusItem *_status;
    JNBrowser *_browser;
    EventHotKeyRef _hotkey, _hotkeyPlus, _hotkeyShiftPlus;
    EventHandlerRef _handler;
    BOOL _launched, _quitting;
    NSURL *_configuration;
    NSInteger _sessionPalette, _nextColor, _colorsLeft;
    unsigned char _colorBag[COLOR_COUNT];
    BOOL _shuffle;
    NSMenu *_tray;
    NSTimer *_trayClick;
    BOOL _recentDirty;

}
@synthesize store=_store;
- (NSInteger)sessionPalette { return _sessionPalette; }
- (void)setStore:(JNStore *)store {
    _recentDirty=NO;
    _store=store; NSString *favorite=store.preferences[@"favorite_palette"];
    NSInteger p=favorite && favorite.integerValue>=0 ? favorite.integerValue : [store.preferences[@"palette"] integerValue];
    _sessionPalette=MAX(0,MIN(PALETTE_COUNT-1,p)); _shuffle=favorite && favorite.integerValue>=0;
    _nextColor=MAX(0,[store.preferences[@"next_color"] integerValue])%PALETTE_SIZES[_sessionPalette]; _colorsLeft=0;
}
- (void)usePalette:(NSInteger)palette {
    if(palette<0 || palette>=PALETTE_COUNT) return;
    if(_sessionPalette!=palette || !_shuffle) _colorsLeft=0;
    _sessionPalette=palette; _shuffle=YES;
}
- (void)defaultPalette:(NSMenuItem *)item {
    if(item.tag<0 || item.tag>=DEFAULT_PALETTE_COUNT) return;
    _sessionPalette=item.tag; _shuffle=NO; _nextColor=_colorsLeft=0;
    self.store.preferences[@"palette"]=@(item.tag).stringValue; self.store.preferences[@"next_color"]=@"0";
    NSError *error=nil; if(![self.store saveMetadata:&error]) JNError(error);
}
- (NSInteger)nextColor {
    if(!_shuffle) return _nextColor;
    if(!_colorsLeft) {
        for(NSInteger i=0;i<PALETTE_SIZES[_sessionPalette];++i) {
            BOOL duplicate=NO; for(NSInteger j=0;j<_colorsLeft;++j) if(PALETTES[_sessionPalette][_colorBag[j]]==PALETTES[_sessionPalette][i]) duplicate=YES;
            if(!duplicate) _colorBag[_colorsLeft++]=(unsigned char)i;
        }
        for(NSInteger i=_colorsLeft-1;i>0;--i) { NSInteger j=arc4random_uniform((uint32_t)i+1); unsigned char t=_colorBag[i]; _colorBag[i]=_colorBag[j]; _colorBag[j]=t; }
    }
    return _colorBag[_colorsLeft-1];
}
- (void)revealArchive:(id)sender { (void)sender; [NSWorkspace.sharedWorkspace openURL:self.store.directory]; }
- (instancetype)init { if ((self=[super init])) _notes=[NSMutableDictionary dictionary]; return self; }
- (NSURL *)pickDirectory {
    NSOpenPanel *panel=[NSOpenPanel openPanel]; panel.canChooseDirectories=YES; panel.canChooseFiles=NO;
    panel.canCreateDirectories=YES; panel.treatsFilePackagesAsDirectories=YES;
    panel.message=self.portable ? @"Choose the portable notes folder. Notes and settings stay in this folder." : @"Open or create a notes archive. The current archive is retained.";
    return [panel runModal]==NSModalResponseOK ? panel.URL : nil;
}
- (void)applicationWillFinishLaunching:(NSNotification *)notification {
    (void)notification;
    self.portable=self.portable || [[NSBundle.mainBundle objectForInfoDictionaryKey:@"JotillaPortable"] boolValue];
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification; NSError *error=nil;
    NSAppleEventDescriptor *launch = [NSAppleEventManager.sharedAppleEventManager.currentAppleEvent paramDescriptorForKeyword:keyAEPropData];
    self.startup = self.startup || launch.enumCodeValue == keyAELaunchedAsLogInItem;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSURL *directory=self.initialDirectory;
    if (!self.portable) {
        NSURL *support=[NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:&error];
        NSURL *root=support ? JNProfileRoot(support) : nil;
        if (!support || ![NSFileManager.defaultManager createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:&error]) { JNError(error); [NSApp terminate:nil]; return; }
        _configuration=[root URLByAppendingPathComponent:@"archive.path"];
        if (!directory) {
            NSString *saved=[NSString stringWithContentsOfURL:_configuration encoding:NSUTF8StringEncoding error:NULL];
            saved=[saved stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            if (saved.length) {
                directory=[NSURL fileURLWithPath:saved isDirectory:YES];
                BOOL isDirectory=NO;
                if (![NSFileManager.defaultManager fileExistsAtPath:directory.path isDirectory:&isDirectory] || !isDirectory) {
                    JNError([NSError errorWithDomain:@"Jotilla" code:1 userInfo:@{NSLocalizedDescriptionKey:@"The configured archive is unavailable. Reconnect its drive. Its location has not been changed."}]); [NSApp terminate:nil]; return;
                }
            } else directory=[root URLByAppendingPathComponent:@"notes" isDirectory:YES];
        }
    }
    if (!directory) directory=[self pickDirectory];
    if (!directory) { [NSApp terminate:nil]; return; }
    self.store=[[JNStore alloc] initWithDirectory:directory error:&error];
    if (!self.store) { JNError(error); [NSApp terminate:nil]; return; }
    [self installMenu];
    EventTypeSpec spec={kEventClassKeyboard,kEventHotKeyPressed};
    OSStatus status=InstallEventHandler(GetApplicationEventTarget(),hotkey,1,&spec,(__bridge void *)self,&_handler);
    if (status==noErr) status=RegisterEventHotKey(kVK_ANSI_N,controlKey|optionKey|cmdKey,(EventHotKeyID){0x4a6f746c,1},GetApplicationEventTarget(),0,&_hotkey);
    RegisterEventHotKey(kVK_ANSI_Equal,controlKey|optionKey,(EventHotKeyID){0x4a6f746c,2},GetApplicationEventTarget(),0,&_hotkeyPlus);
    RegisterEventHotKey(kVK_ANSI_Equal,controlKey|optionKey|shiftKey,(EventHotKeyID){0x4a6f746c,3},GetApplicationEventTarget(),0,&_hotkeyShiftPlus);
    if (status!=noErr) NSLog(@"Jotilla: global shortcut unavailable (%d); use the menu bar item.",(int)status);
    /* Restoring an existing session never activates the app or creates a note. */
    [self restore]; _launched=YES;
    if (!self.store.records.count && !self.startup) [self newNote:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(screenChanged:) name:NSApplicationDidChangeScreenParametersNotification object:nil];
}
- (void)restore {
    for (JNRecord *record in self.store.records)
        if (!record.settings[@"open"] || [record.settings[@"open"] boolValue]) [self openRecord:record activate:NO];
    if (!self.store.preferences[@"last_note"].length && self.notes.count) {
        NSString *identifier=[[self.notes.allKeys sortedArrayUsingSelector:@selector(compare:)] lastObject];
        [self rememberNote:self.notes[identifier]];
    }
}
- (void)installMenu {
    NSMenu *main=[NSMenu new], *appMenu=[NSMenu new], *file=[NSMenu new], *edit=[NSMenu new], *format=[NSMenu new];
    NSArray *menus=@[appMenu,file,edit,format]; NSArray *titles=@[@"Jotilla",@"File",@"Edit",@"Format"];
    for (NSUInteger i=0;i<menus.count;++i) [main addItemWithTitle:titles[i] action:nil keyEquivalent:@""].submenu=menus[i];
    [appMenu addItemWithTitle:@"About Jotilla" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    NSMenuItem *login=[appMenu addItemWithTitle:@"Start at login" action:@selector(toggleLogin:) keyEquivalent:@""]; login.target=self;
    login.state=SMAppService.mainAppService.status==SMAppServiceStatusEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    [appMenu addItem:NSMenuItem.separatorItem]; [appMenu addItemWithTitle:@"Quit Jotilla" action:@selector(terminate:) keyEquivalent:@"q"];
    NSArray *commands=@[@"New note",@"Note explorer",@"Search notes",@"Open archive…",@"Reload external edits"];
    NSArray *keys=@[@"n",@"e",@"f",@"o",@"r"];
    SEL actions[]={@selector(newNote:),@selector(browse:),@selector(search:),@selector(chooseArchive:),@selector(reload:)};
    for (NSUInteger i=0;i<commands.count;++i) { NSMenuItem *item=[file addItemWithTitle:commands[i] action:actions[i] keyEquivalent:keys[i]]; item.target=self; }
    NSArray *editNames=@[@"Undo",@"Redo",@"Cut",@"Copy",@"Paste",@"Select all"];
    NSArray *editKeys=@[@"z",@"Z",@"x",@"c",@"v",@"a"];
    SEL editActions[]={@selector(undo:),@selector(redo:),@selector(cut:),@selector(copy:),@selector(paste:),@selector(selectAll:)};
    for (NSUInteger i=0;i<editNames.count;++i) [edit addItemWithTitle:editNames[i] action:editActions[i] keyEquivalent:editKeys[i]];
    NSArray *styleNames=@[@"Bold",@"Italic",@"Underline",@"Strikethrough",@"Increase size",@"Decrease size",@"Tags…",@"Assign day…",@"Rename…",@"Fold / expand"];
    NSArray *styleKeys=@[@"b",@"i",@"u",@"X",@"+",@"-",@"t",@"d",@"",@""];
    for (NSUInteger i=0;i<styleNames.count;++i) { NSMenuItem *item=[format addItemWithTitle:styleNames[i] action:@selector(noteCommand:) keyEquivalent:styleKeys[i]]; item.target=self; item.tag=(NSInteger)i+1; }
    NSMenuItem *palette=[format addItemWithTitle:@"Color palette…" action:@selector(paletteCommand:) keyEquivalent:@"p"]; palette.target=self;
    NSMenuItem *save=[file addItemWithTitle:@"Save all notes" action:@selector(saveAll:) keyEquivalent:@"s"]; save.target=self;
    [edit addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"y"].hidden=YES;
    NSMenuItem *findAlias=[file addItemWithTitle:@"Search notes" action:@selector(search:) keyEquivalent:@"F"]; findAlias.target=self; findAlias.hidden=YES;
    NSApp.mainMenu=main;
    _status=[NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
    _status.autosaveName=@"JotilaMenuBar"; _status.visible=YES; /* Stable menu-bar position preference. */
    _status.button.title=@""; _status.button.image=statusIcon(); _status.button.imagePosition=NSImageOnly;
    _status.button.toolTip=@"Jotilla — last note; double-click for explorer"; [_status.button setAccessibilityLabel:@"Jotilla notes"];
    NSMenu *tray=[file copy]; [tray addItem:NSMenuItem.separatorItem]; [tray addItemWithTitle:@"Quit Jotilla" action:@selector(terminate:) keyEquivalent:@"q"]; _tray=tray;
    _status.button.target=self; _status.button.action=@selector(statusClicked:); [_status.button sendActionOn:NSEventMaskLeftMouseUp|NSEventMaskRightMouseUp];
}
- (void)statusClicked:(id)sender {
    (void)sender; NSEvent *event=NSApp.currentEvent;
    if(event.type==NSEventTypeRightMouseUp || (event.modifierFlags&NSEventModifierFlagControl)) {
        [_trayClick invalidate]; [_tray popUpMenuPositioningItem:nil atLocation:NSMakePoint(0,0) inView:_status.button];
    /* Accessibility/keyboard activation may have a non-mouse current event;
       asking that event for clickCount raises an AppKit exception. */
    } else if(event.type==NSEventTypeLeftMouseUp && event.clickCount>1) { [_trayClick invalidate]; [self browse:nil]; }
    else { [_trayClick invalidate]; __weak JNApp *weakSelf=self; _trayClick=[NSTimer scheduledTimerWithTimeInterval:NSEvent.doubleClickInterval repeats:NO block:^(NSTimer *timer) { (void)timer; [weakSelf recallNote:nil]; }]; }
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action==@selector(toggleLogin:)) return !self.portable && [NSBundle.mainBundle.bundlePath hasPrefix:@"/Applications/"];
    if (item.action==@selector(noteCommand:)) return [NSApp.keyWindow.windowController isKindOfClass:JNNote.class];
    return YES;
}
- (void)toggleLogin:(NSMenuItem *)item {
    NSError *error=nil; SMAppService *service=SMAppService.mainAppService;
    BOOL ok=service.status==SMAppServiceStatusEnabled ? [service unregisterAndReturnError:&error] : [service registerAndReturnError:&error];
    if (!ok) JNError(error);
    item.state=service.status==SMAppServiceStatusEnabled ? NSControlStateValueOn : NSControlStateValueOff;
    if (service.status==SMAppServiceStatusRequiresApproval) [SMAppService openSystemSettingsLoginItems];
}
- (void)paletteCommand:(id)sender { (void)sender; JNNote *note=(JNNote *)NSApp.keyWindow.windowController; if([note isKindOfClass:JNNote.class]) [note showPalette:nil]; }
- (void)saveAll:(id)sender { (void)sender; NSError *error=nil; if(![self flush:&error]) JNError(error); }
- (void)noteCommand:(NSMenuItem *)item {
    JNNote *note=(JNNote *)NSApp.keyWindow.windowController;
    if (![note isKindOfClass:JNNote.class]) return;
    if (item.tag<=4) [note format:item];
    else if (item.tag<=6) [note resizeBy:item.tag==5 ? 1 : -1];
    else if (item.tag==7) [note tags:nil];
    else if (item.tag==8) [note chooseDate:nil];
    else if (item.tag==9) [note rename:nil];
    else [note fold:nil];
}
- (void)newNote:(id)sender {
    if (!self.store) return;
    JNRecord *record=[self.store newRecord];
    record.settings[@"palette"]=@(_sessionPalette).stringValue;
    record.settings[@"color"]=@([self nextColor]).stringValue;
    NSError *error=nil; JNNote *note=[[JNNote alloc] initWithRecord:record app:self fresh:YES error:&error];
    if (!note) { JNError(error); return; }
    self.notes[record.identifier]=note;
    if(_shuffle) --_colorsLeft; else { _nextColor=(_nextColor+1)%PALETTE_SIZES[_sessionPalette]; self.store.preferences[@"next_color"]=@(_nextColor).stringValue; }
    [note showAtPointer:[sender isKindOfClass:NSString.class] activate:YES];
}
- (void)rememberNote:(JNNote *)note {
    if (![self.store.preferences[@"last_note"] isEqual:note.record.identifier]) {
        self.store.preferences[@"last_note"]=note.record.identifier;
        _recentDirty=YES;
    }
}
- (void)recallNote:(id)sender {
    (void)sender;
    NSString *identifier=self.store.preferences[@"last_note"];
    JNNote *note=identifier ? self.notes[identifier] : nil;
    if (note) {
        if (note.collapsed) [note fold:nil];
        [note showAtPointer:NO activate:YES]; return;
    }
    for (JNRecord *record in self.store.records) if ([record.identifier isEqual:identifier]) {
        [self openRecord:record activate:YES];
        note=self.notes[identifier];
        if (note.collapsed) [note fold:nil];
        return;
    }
    [self browse:nil];
}
- (void)openRecord:(JNRecord *)record activate:(BOOL)activate {
    if (!record) return;
    JNNote *note=self.notes[record.identifier];
    if (!note) {
        NSError *error=nil; note=[[JNNote alloc] initWithRecord:record app:self fresh:NO error:&error];
        if (!note) { JNError(error); return; }
        self.notes[record.identifier]=note;
        if (activate) { record.settings[@"open"]=@"1"; if (![self.store saveMetadata:&error]) JNError(error); }
    }
    [note showAtPointer:NO activate:activate];
}
- (void)closeNote:(JNNote *)note {
    /* Retain the controller until the window's close callback has returned. */
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.notes[note.record.identifier] == note) [self.notes removeObjectForKey:note.record.identifier];
        [self changed];
    });
}
- (void)changed { [_browser refresh]; }
- (void)changedRecord:(JNRecord *)record { [_browser recordChanged:record]; }
- (void)browse:(id)sender {
    (void)sender; NSError *error=nil; if (![self flush:&error]) { JNError(error); return; }
    if (!_browser) _browser=[[JNBrowser alloc] initWithApp:self];
    JNActivate(); [_browser showNotes]; [_browser showWindow:nil]; [_browser.window makeKeyAndOrderFront:nil];
}
- (void)search:(id)sender { [self browse:sender]; [_browser focusSearch]; }
- (BOOL)flush:(NSError **)error {
    for (JNNote *note in self.notes.allValues) if (![note flush:error]) return NO;
    if (_recentDirty) {
        if (![self.store saveMetadata:error]) return NO;
        _recentDirty=NO;
    }
    return YES;
}
- (BOOL)switchDirectory:(NSURL *)directory {
    if ([directory.URLByResolvingSymlinksInPath.URLByStandardizingPath isEqual:self.store.directory]) return YES;
    NSError *error=nil;
    if (![self flush:&error]) { JNError(error); return NO; }
    JNStore *next=[[JNStore alloc] initWithDirectory:directory error:&error];
    if (!next) { JNError(error); return NO; }
    if (_configuration && !JNWrite([next.directory.path dataUsingEncoding:NSUTF8StringEncoding],_configuration,&error)) { JNError(error); return NO; }
    for (JNNote *note in self.notes.allValues) [note detach];
    [self.notes removeAllObjects]; self.store=next; [_browser invalidate]; [self restore]; [self changed]; return YES;
}
- (void)chooseArchive:(id)sender {
    (void)sender; NSAlert *alert=[NSAlert new]; alert.messageText=@"Choose where your notes live"; alert.informativeText=self.store.directory.path;
    [alert addButtonWithTitle:@"Open or create archive"]; [alert addButtonWithTitle:@"Move current archive"]; [alert addButtonWithTitle:@"Cancel"];
    NSModalResponse response=[alert runModal]; if(response==NSAlertThirdButtonReturn) return;
    NSURL *directory=[self pickDirectory]; if(!directory) return;
    if(response==NSAlertFirstButtonReturn) { if([self switchDirectory:directory]) [self browse:nil]; return; }
    NSError *error=nil; if(![self flush:&error]) { JNError(error); return; }
    JNStore *previous=self.store, *next=[previous copyToDirectory:directory error:&error];
    if(!next) { JNError(error); return; }
    if(_configuration && !JNWrite([next.directory.path dataUsingEncoding:NSUTF8StringEncoding],_configuration,&error)) { JNError(error); return; }
    for(JNNote *note in self.notes.allValues) [note detach]; [self.notes removeAllObjects]; self.store=next;
    if(![previous removeTransferredFiles:next error:&error]) JNError(error);
    [_browser invalidate]; [self restore]; [self browse:nil];
}
- (void)reload:(id)sender {
    (void)sender; NSError *error=nil;
    if (![self flush:&error]) {
        NSAlert *alert=[NSAlert new]; alert.messageText=@"Reload and discard unsaved edits?";
        alert.informativeText=[error.localizedDescription stringByAppendingString:@"\nCancel to export an RTF copy from the note menu first."];
        [alert addButtonWithTitle:@"Cancel"]; [alert addButtonWithTitle:@"Discard and reload"];
        if ([alert runModal]!=NSAlertSecondButtonReturn) return;
    }
    /* Close only after the new snapshot has been read successfully. */
    if (![self.store reload:&error]) { JNError(error); return; }
    for (JNNote *note in self.notes.allValues) [note detach];
    [self.notes removeAllObjects]; [_browser invalidate]; [self restore]; [self changed];
}
- (void)screenChanged:(NSNotification *)notification {
    (void)notification;
    if(_browser.window.visible) [_browser.window setFrame:JNConstrainFrame(_browser.window.frame,(_browser.window.screen ?: NSScreen.mainScreen).visibleFrame) display:YES];
    for (JNNote *note in self.notes.allValues) {
        NSRect frame=note.window.frame; NSScreen *target=NSScreen.mainScreen;
        for(NSScreen *screen in NSScreen.screens) if(NSIntersectsRect(NSMakeRect(frame.origin.x,NSMaxY(frame)-32,frame.size.width,32),screen.visibleFrame)) { target=screen; break; }
        if(target) [note.window setFrame:JNConstrainFrame(frame,target.visibleFrame) display:YES];
    }
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)app hasVisibleWindows:(BOOL)visible { (void)app; (void)visible; [self browse:nil]; return NO; }
- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)urls {
    (void)application; if (!urls.count) return;
    if (!_launched) self.initialDirectory=urls.firstObject;
    else [self switchDirectory:urls.firstObject];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender; NSError *error=nil;
    if (![self flush:&error]) { JNError(error); return NSTerminateCancel; }
    _quitting=YES; return NSTerminateNow;
}
- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification; (void)_quitting;
    if (_hotkey) UnregisterEventHotKey(_hotkey);
    if (_hotkeyPlus) UnregisterEventHotKey(_hotkeyPlus);
    if (_hotkeyShiftPlus) UnregisterEventHotKey(_hotkeyShiftPlus);
    if (_handler) RemoveEventHandler(_handler);
    [NSNotificationCenter.defaultCenter removeObserver:self];
}
@end
