#import "Store.h"
#include "../colors.h"
#include "../date.h"

NS_ASSUME_NONNULL_BEGIN
@class JNApp, JNBrowser;
FOUNDATION_EXPORT NSColor *JNColor(COLORREF color);
FOUNDATION_EXPORT void JNError(NSError * _Nullable error);
FOUNDATION_EXPORT void JNActivate(void);
FOUNDATION_EXPORT NSString * _Nullable JNAsk(NSString *title, NSString *value);
FOUNDATION_EXPORT NSFont *JNFont(CGFloat size, BOOL bold);
FOUNDATION_EXPORT NSString *JNPaletteName(NSInteger index);
FOUNDATION_EXPORT NSRect JNConstrainFrame(NSRect frame, NSRect visible);

@interface JNNote : NSWindowController <NSWindowDelegate, NSTextViewDelegate>
@property(nonatomic, strong, readonly) JNRecord *record;
@property(nonatomic, strong, readonly) NSTextView *editor;
@property(nonatomic, weak) JNApp *app;
@property(nonatomic, readonly) BOOL scrolling;
@property(nonatomic, readonly) BOOL collapsed;
- (void)toggleScrolling:(nullable id)sender;
- (nullable instancetype)initWithRecord:(JNRecord *)record app:(JNApp *)app fresh:(BOOL)fresh error:(NSError * _Nullable * _Nullable)error;
- (BOOL)flush:(NSError * _Nullable * _Nullable)error;
- (void)showAtPointer:(BOOL)pointer activate:(BOOL)activate;
- (void)fold:(nullable id)sender;
- (void)tags:(nullable id)sender;
- (void)chooseDate:(nullable id)sender;
- (void)rename:(nullable id)sender;
- (void)format:(id)sender;
- (void)resizeBy:(CGFloat)amount;
- (void)detach;
- (NSMenu *)contextMenu;
- (void)showPalette:(nullable id)sender;
- (void)selectColor:(NSMenuItem *)item;
- (void)favorite:(NSMenuItem *)item;
- (void)toggleChecklistAt:(NSUInteger)index;
- (BOOL)insertNewlineWithShift:(BOOL)shift;
- (BOOL)resizeTo:(NSSize)size;
@end

@interface JNBrowser : NSWindowController
@property(nonatomic, weak) JNApp *app;
- (instancetype)initWithApp:(JNApp *)app;
- (void)refresh;
- (void)focusSearch;
- (void)showNotes;
- (void)invalidate;
- (void)recordChanged:(JNRecord *)record;
@end

@interface JNApp : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) JNStore *store;
@property(nonatomic, strong, readonly) NSMutableDictionary<NSString *, JNNote *> *notes;
@property(nonatomic) BOOL portable;
@property(nonatomic) BOOL startup;
@property(nonatomic, strong, nullable) NSURL *initialDirectory;
@property(nonatomic, readonly) NSInteger sessionPalette;
- (void)usePalette:(NSInteger)palette;
- (void)defaultPalette:(NSMenuItem *)sender;
- (void)revealArchive:(nullable id)sender;
- (void)newNote:(nullable id)sender;
- (void)recallNote:(nullable id)sender;
- (void)rememberNote:(JNNote *)note;
- (void)browse:(nullable id)sender;
- (void)search:(nullable id)sender;
- (void)chooseArchive:(nullable id)sender;
- (void)reload:(nullable id)sender;
- (BOOL)flush:(NSError * _Nullable * _Nullable)error;
- (void)openRecord:(JNRecord *)record activate:(BOOL)activate;
- (void)changed;
- (void)changedRecord:(JNRecord *)record;
- (void)closeNote:(JNNote *)note;
@end
NS_ASSUME_NONNULL_END
