#ifdef WNP_BUILD_PLATFORM_DARWIN

#include "internal.h"
#include "wnp.h"
#include <Foundation/Foundation.h>

typedef enum {
  MRMediaRemoteCommandPlay = 0,
  MRMediaRemoteCommandPause = 1,
  MRMediaRemoteCommandStop = 3,
  MRMediaRemoteCommandNextTrack = 4,
  MRMediaRemoteCommandPreviousTrack = 5,
} MRMediaRemoteCommand;

void MRMediaRemoteRegisterForNowPlayingNotifications(dispatch_queue_t queue);
void MRMediaRemoteUnregisterForNowPlayingNotifications(void);
typedef void (^MRMediaRemoteGetNowPlayingInfoCallback)(NSDictionary* info);
void MRMediaRemoteGetNowPlayingInfo(dispatch_queue_t queue, MRMediaRemoteGetNowPlayingInfoCallback block);
typedef void (^MRMediaRemoteGetNowPlayingApplicationIsPlayingCallback)(BOOL isPlaying);
void MRMediaRemoteGetNowPlayingApplicationIsPlaying(dispatch_queue_t queue, MRMediaRemoteGetNowPlayingApplicationIsPlayingCallback block);
void MRMediaRemoteSetElapsedTime(double time);
Boolean MRMediaRemoteSendCommand(MRMediaRemoteCommand command, NSDictionary* userInfo);

extern NSString* kMRMediaRemoteNowPlayingApplicationDisplayNameUserInfoKey;
extern NSString* kMRMediaRemoteNowPlayingApplicationIsPlayingUserInfoKey;

extern NSString* kMRMediaRemoteNowPlayingInfoArtist;
extern NSString* kMRMediaRemoteNowPlayingInfoTitle;
extern NSString* kMRMediaRemoteNowPlayingInfoAlbum;
extern NSString* kMRMediaRemoteNowPlayingInfoArtworkData;
extern NSString* kMRMediaRemoteNowPlayingInfoDuration;
extern NSString* kMRMediaRemoteNowPlayingInfoElapsedTime;
extern NSString* kMRMediaRemoteNowPlayingInfoShuffleMode;

static uint64_t _darwin_timestamp()
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint64_t milliseconds = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
  return milliseconds;
}

static void _darwin_assign_str(char dest[WNP_STR_LEN], const char* str)
{
  size_t len = strlen(str);
  strncpy(dest, str, WNP_STR_LEN - 1);
  dest[len < WNP_STR_LEN ? len : WNP_STR_LEN - 1] = '\0';
}

@interface DarwinPlayer : NSObject

@property int player_id;
@property BOOL isPlaying;
@property BOOL is_web_browser;
@property BOOL old_is_web_browser;
@property int old_position;
@property(copy) NSString* old_title;
@property int old_is_playing;

- (void)play;
- (void)pause;
- (void)stop;
- (void)skip_next;
- (void)skip_previous;
- (void)set_position:(int)seconds;

@end

@interface DarwinPlayer (Private)
- (void)appDidChange:(NSNotification*)notification;
- (void)infoDidChange:(NSNotification*)notification;
- (void)isPlayingDidChange:(NSNotification*)notification;
- (void)getNowPlayingInfo;
@end

static void commonInit(DarwinPlayer* self, int player_id)
{
  self.old_is_web_browser = FALSE;
  self.is_web_browser = FALSE;
  self.old_position = -1;
  self.old_title = @"no";
  self.old_is_playing = FALSE;
  self.player_id = player_id;

  CFURLRef ref = (__bridge CFURLRef)[NSURL fileURLWithPath:@"/System/Library/PrivateFrameworks/MediaRemote.framework"];
  CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, ref);

  NSString* kMRMediaRemoteNowPlayingApplicationDidChangeNotification =
      (__bridge NSString*)CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("kMRMediaRemoteNowPlayingApplicationDidChangeNotification"));
  NSString* kMRMediaRemoteNowPlayingInfoDidChangeNotification =
      (__bridge NSString*)CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("kMRMediaRemoteNowPlayingInfoDidChangeNotification"));
  NSString* kMRMediaRemoteNowPlayingApplicationIsPlayingDidChangeNotification =
      (__bridge NSString*)CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("kMRMediaRemoteNowPlayingApplicationIsPlayingDidChangeNotification"));

  [NSNotificationCenter.defaultCenter addObserver:self
                                         selector:@selector(appDidChange:)
                                             name:kMRMediaRemoteNowPlayingApplicationDidChangeNotification
                                           object:nil];
  [NSNotificationCenter.defaultCenter addObserver:self
                                         selector:@selector(infoDidChange:)
                                             name:kMRMediaRemoteNowPlayingInfoDidChangeNotification
                                           object:nil];
  [NSNotificationCenter.defaultCenter addObserver:self
                                         selector:@selector(isPlayingDidChange:)
                                             name:kMRMediaRemoteNowPlayingApplicationIsPlayingDidChangeNotification
                                           object:nil];

  MRMediaRemoteGetNowPlayingApplicationIsPlaying(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(BOOL isPlaying) {
    self.isPlaying = isPlaying;
  });

  [self getNowPlayingInfo];
}

@implementation DarwinPlayer

- (void)getNowPlayingInfo
{
  MRMediaRemoteGetNowPlayingApplicationIsPlaying(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(BOOL isPlaying) {
    self.isPlaying = isPlaying;
  });
  MRMediaRemoteGetNowPlayingInfo(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^(NSDictionary* info) {
    NSString* ns_title = [info objectForKey:kMRMediaRemoteNowPlayingInfoTitle];
    int position = round([[info objectForKey:kMRMediaRemoteNowPlayingInfoElapsedTime] doubleValue]);
    if (self.old_is_web_browser == self.is_web_browser && self.old_is_playing == self.isPlaying && self.old_position == position &&
        [self.old_title isEqualToString:ns_title]) {
      return;
    } else {
      self.old_is_web_browser = self.is_web_browser;
      self.old_is_playing = self.isPlaying;
      self.old_position = position;
      self.old_title = [ns_title copy];
    }

    wnp_player_t players[WNP_MAX_PLAYERS];
    int count = __wnp_start_update_cycle(players);
    wnp_player_t* player = &players[self.player_id];
    if (player == NULL || player->id != self.player_id) {
      __wnp_end_update_cycle();
      return;
    }

    player->is_web_browser = self.is_web_browser;
    player->state = self.isPlaying ? WNP_STATE_PLAYING : WNP_STATE_PAUSED;

    const char* title = [[info objectForKey:kMRMediaRemoteNowPlayingInfoTitle] UTF8String];
    const char* artist = [[info objectForKey:kMRMediaRemoteNowPlayingInfoArtist] UTF8String];
    const char* album = [[info objectForKey:kMRMediaRemoteNowPlayingInfoAlbum] UTF8String];
    NSNumber* duration = [info objectForKey:kMRMediaRemoteNowPlayingInfoDuration];

    // TODO: artwork
    NSString* artworkData = [info objectForKey:kMRMediaRemoteNowPlayingInfoArtworkData];

    _darwin_assign_str(player->artist, artist);
    _darwin_assign_str(player->album, album);
    player->duration = duration == nil ? 0 : round([duration doubleValue]);
    player->position = position;

    if (strcmp(player->title, title) != 0) {
      _darwin_assign_str(player->title, title);
      if (strlen(title) == 0) {
        player->active_at = 0;
      } else {
        player->active_at = _darwin_timestamp();
      }
    }

    player->updated_at = _darwin_timestamp();

    __wnp_update_player(player);
    __wnp_end_update_cycle();
  });
}

- (instancetype)init:(int)player_id
{
  self = [super init];
  if (self) commonInit(self, player_id);
  return self;
}

- (void)dealloc
{
  [super dealloc];
  [NSNotificationCenter.defaultCenter removeObserver:self];
  MRMediaRemoteUnregisterForNowPlayingNotifications();
}

- (void)isPlayingDidChange:(NSNotification*)notification
{
  self.isPlaying = [[notification.userInfo objectForKey:kMRMediaRemoteNowPlayingApplicationIsPlayingUserInfoKey] boolValue];

  [self getNowPlayingInfo];
}

- (void)infoDidChange:(NSNotification*)notification
{
  NSDictionary* info = notification.userInfo;
  NSString* name = [info objectForKey:kMRMediaRemoteNowPlayingApplicationDisplayNameUserInfoKey];

  if (name != nil) {
    // TODO: get other browser names, though i'd believe it'd just be "chrome" "firefox" etc?
    if ([name caseInsensitiveCompare:@"arc"] == NSOrderedSame) {
      self.is_web_browser = TRUE;
    } else {
      self.is_web_browser = FALSE;
    }
  } else {
    self.is_web_browser = FALSE;
  }

  [self getNowPlayingInfo];
}

- (void)appDidChange:(NSNotification*)notification
{
  [self getNowPlayingInfo];
}

- (void)play
{
  MRMediaRemoteSendCommand(MRMediaRemoteCommandPlay, nil);
}

- (void)pause
{
  MRMediaRemoteSendCommand(MRMediaRemoteCommandPause, nil);
}

- (void)stop
{
  MRMediaRemoteSendCommand(MRMediaRemoteCommandStop, nil);
}

- (void)skip_next
{
  MRMediaRemoteSendCommand(MRMediaRemoteCommandNextTrack, nil);
}

- (void)skip_previous
{
  MRMediaRemoteSendCommand(MRMediaRemoteCommandPreviousTrack, nil);
}

- (void)set_position:(int)seconds
{
  MRMediaRemoteSetElapsedTime(seconds);
}

@end

typedef struct {
  DarwinPlayer* player;
} _darwin_state_t;

static _darwin_state_t _darwin_state = {0};

wnp_init_ret_t __wnp_platform_darwin_init()
{
  __wnp_start_update_cycle(NULL);
  wnp_player_t player = WNP_DEFAULT_PLAYER;
  _darwin_assign_str(player.name, "MacOS");
  player.platform = WNP_PLATFORM_DARWIN;
  player.can_set_state = true;
  player.can_skip_previous = true;
  player.can_skip_next = true;
  player.can_set_position = true;
  player.created_at = _darwin_timestamp();
  player.id = __wnp_add_player(&player);
  if (player.id == -1) {
    __wnp_end_update_cycle();
    return WNP_INIT_DARWIN_FAILED;
  }
  __wnp_end_update_cycle();

  _darwin_state.player = [[DarwinPlayer alloc] init:(int)player.id];
  return WNP_INIT_SUCCESS;
}

void __wnp_platform_darwin_uninit()
{
  [_darwin_state.player dealloc];
}

void __wnp_platform_darwin_free(void* platform_data)
{
}

void __wnp_platform_darwin_event(wnp_player_t* player, wnp_event_t event, int event_id, int data)
{
  switch (event) {
    case WNP_TRY_SET_STATE:
      player->state = data;
      switch ((wnp_state_t)data) {
        case WNP_STATE_PLAYING:
          [_darwin_state.player play];
          break;
        case WNP_STATE_PAUSED:
          [_darwin_state.player pause];
          break;
        case WNP_STATE_STOPPED:
          [_darwin_state.player stop];
          break;
      }
      break;
    case WNP_TRY_SKIP_PREVIOUS:
      [_darwin_state.player skip_previous];
      break;
    case WNP_TRY_SKIP_NEXT:
      [_darwin_state.player skip_next];
      break;
    case WNP_TRY_SET_POSITION:
      [_darwin_state.player set_position:(int)data];
      player->position = data;
      break;
    case WNP_TRY_SET_VOLUME:
    case WNP_TRY_SET_RATING:
    case WNP_TRY_SET_REPEAT:
    case WNP_TRY_SET_SHUFFLE:
      break;
  }

  __wnp_set_event_result(event_id, WNP_EVENT_SUCCEEDED);
}

#endif /* WNP_BUILD_PLATFORM_DARWIN */
