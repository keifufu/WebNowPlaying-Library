from . import wnp_wrapper




######################################
######################################
####                              ####
####       Basic Functions        ####
####                              ####
######################################
######################################

def start(port:int, adapter_version:str, events:wnp_wrapper.wnp_events=None):
    """
    Start the WebNowPlaying adapter.

    Args:
        port (int): The port to start the adapter on. (Required)
        adapter_version (str): The version of the adapter. (Required)
        events (wnp_wrapper.wnp_events): The events to use for the adapter. (Default: None)

    Returns:
        Result of the operation.

    Raises:
        None.
        
    """
    return wnp_wrapper.wnp_start(port, adapter_version, events)

def stop():
    """
    Stop the WebNowPlaying adapter.

    Args:
        None.

    Returns:
        Result of the operation.

    Raises:
        None.
        
    """
    return wnp_wrapper.wnp_stop()

def get_state():
    """
    Get the state of the WebNowPlaying adapter.

    Args:
        None.

    Returns:
        The state of the WebNowPlaying adapter.

    Raises:
        None.
        
    """
    return wnp_wrapper.wnp_is_started()

def get_player(id:int, always_return_player:bool):
    """
    Get the player with the specified ID.

    Parameters:
    id (int): The ID of the player. (Required)
    always_return_player (bool): Whether to always return a player, even if it doesn't exist. (Required)

    Returns:
    Player: The player object.

    """
    return wnp_wrapper.wnp_get_player(id, always_return_player)

def get_active_player(always_return_player:bool):
    """
    Get the active player.

    Parameters:
    always_return_player (bool): Whether to always return a player, even if it doesn't exist. (Required)

    Returns:
    Player: The active player object.

    """
    return wnp_wrapper.wnp_get_active_player(always_return_player)

def get_all_players(players):
    """
    Get all players.

    Parameters:
    players: The players object to fill. (Required)

    Returns:
    None.

    """
    return wnp_wrapper.wnp_get_all_players(players)




######################################
######################################
####                              ####
####         Player Info          ####
####                              ####
######################################
######################################

class MediaInfo:
    """
    Represents the information about the currently playing media.
    """

    def __init__(self):
        self.player = wnp_wrapper.wnp_get_active_player(True)

    @property
    def id(self):
        """
        Get the ID of the player.
        
        Returns:
            int: The ID of the player.
        """
        return self.player.id

    @property
    def name(self):
        """
        Get the name of the player.
        
        Returns:
            str: The name of the player.
        """
        return self.player.name

    @property
    def title(self):
        """
        Get the title of the player.
        
        Returns:
            str: The title of the player.
        """
        return self.player.title

    @property
    def artist(self):
        """
        Get the artist of the media.
        
        Returns:
            str: The artist of the media.
        """
        return self.player.artist

    @property
    def album(self):
        """
        Get the album of the media.
        
        Returns:
            str: The album of the media.
        """
        return self.player.album

    @property
    def cover(self):
        """
        Get the cover image URL of the media.
        
        Returns:
            str: The cover image URL of the media.
        """
        return self.player.cover

    @property
    def cover_src(self):
        """
        Get the source of the cover image of the media.
        
        Returns:
            str: The source of the cover image of the media.
        """
        return self.player.cover_src

    @property
    def state(self):
        """
        Get the current state of the media (e.g., playing, paused).
        
        Returns:
            str: The current state of the media.
        """
        return self.player.state

    @property
    def position(self):
        """
        Get the current position of the media in seconds.
        
        Returns:
            float: The current position of the media.
        """
        return self.player.position

    @property
    def duration(self):
        """
        Get the total duration of the media in seconds.
        
        Returns:
            float: The total duration of the media.
        """
        return self.player.duration

    @property
    def volume(self):
        """
        Get the current volume level of the media.
        
        Returns:
            float: The current volume level of the media.
        """
        return self.player.volume

    @property
    def rating(self):
        """
        Get the rating of the media.
        
        Returns:
            float: The rating of the media.
        """
        return self.player.rating

    @property
    def repeat(self):
        """
        Check if the media is set to repeat.
        
        Returns:
            bool: True if the media is set to repeat, False otherwise.
        """
        return self.player.repeat

    @property
    def shuffle(self):
        """
        Check if the media is set to shuffle.
        
        Returns:
            bool: True if the media is set to shuffle, False otherwise.
        """
        return self.player.shuffle

    @property
    def rating_system(self):
        """
        Get the rating system used for the media.
        
        Returns:
            str: The rating system used for the media.
        """
        return self.player.rating_system

    @property
    def available_repeat(self):
        """
        Get the available repeat options for the media.
        
        Returns:
            list: The available repeat options for the media.
        """
        return self.player.available_repeat

    @property
    def can_set_state(self):
        """
        Check if the state of the media can be set.
        
        Returns:
            bool: True if the state of the media can be set, False otherwise.
        """
        return self.player.can_set_state

    @property
    def can_skip_previous(self):
        """
        Check if the media can be skipped to the previous track.
        
        Returns:
            bool: True if the media can be skipped to the previous track, False otherwise.
        """
        return self.player.can_skip_previous

    @property
    def can_skip_next(self):
        """
        Check if the media can be skipped to the next track.
        
        Returns:
            bool: True if the media can be skipped to the next track, False otherwise.
        """
        return self.player.can_skip_next

    @property
    def can_set_position(self):
        """
        Check if the position of the media can be set.
        
        Returns:
            bool: True if the position of the media can be set, False otherwise.
        """
        return self.player.can_set_position

    @property
    def can_set_volume(self):
        """
        Check if the volume of the media can be set.
        
        Returns:
            bool: True if the volume of the media can be set, False otherwise.
        """
        return self.player.can_set_volume

    @property
    def can_set_rating(self):
        """
        Check if the rating of the media can be set.
        
        Returns:
            bool: True if the rating of the media can be set, False otherwise.
        """
        return self.player.can_set_rating

    @property
    def can_set_repeat(self):
        """
        Check if the repeat option of the media can be set.
        
        Returns:
            bool: True if the repeat option of the media can be set, False otherwise.
        """
        return self.player.can_set_repeat

    @property
    def can_set_shuffle(self):
        """
        Check if the shuffle option of the media can be set.
        
        Returns:
            bool: True if the shuffle option of the media can be set, False otherwise.
        """
        return self.player.can_set_shuffle

    @property
    def created_at(self):
        """
        Get the timestamp when the player was created.
        
        Returns:
            str: The timestamp when the player was created.
        """
        return self.player.created_at

    @property
    def updated_at(self):
        """
        Get the timestamp when the player was last updated.
        
        Returns:
            str: The timestamp when the player was last updated.
        """
        return self.player.updated_at

    @property
    def active_at(self):
        """
        Get the timestamp when the player was last active.
        
        Returns:
            str: The timestamp when the player was last active.
        """
        return self.player.active_at

    @property
    def is_desktop_player(self):
        """
        Check if the player is a desktop player.
        
        Returns:
            bool: True if the player is a desktop player, False otherwise.
        """
        return self.player.is_desktop_player




######################################
######################################
####                              ####
####        Player Control        ####
####                              ####
######################################
######################################

class MediaControls:
    def __init__(self, player):
        self.player = player

    def try_set_state(self, state):
        """
        Try to set the state of the media player.

        Args:
            state (str): The desired state (e.g., "play", "pause").

        Returns:
            bool: True if the state change was successful, False otherwise.
        """
        return wnp_wrapper.wnp_try_set_state(self.player, state)

    def try_skip_previous(self):
        """
        Try to skip to the previous track.

        Returns:
            bool: True if the operation was successful, False otherwise.
        """
        return wnp_wrapper.wnp_try_skip_previous(self.player)

    def try_skip_next(self):
        """
        Try to skip to the next track.

        Returns:
            bool: True if the operation was successful, False otherwise.
        """
        return wnp_wrapper.wnp_try_skip_next(self.player)

    def try_set_position(self, seconds):
        """
        Try to set the position of the media player.

        Args:
            seconds (float): The desired position in seconds.

        Returns:
            bool: True if the operation was successful, False otherwise.
        """
        return wnp_wrapper.wnp_try_set_position(self.player, seconds)

    def try_set_volume(self, volume):
        """
        Try to set the volume of the media player.

        Args:
            volume (float): The desired volume level.

        Returns:
            bool: True if the operation was successful, False otherwise.
        """
        return wnp_wrapper.wnp_try_set_volume(self.player, volume)

    def try_set_rating(self, rating):
        """
        Try to set the rating of the media player.

        Args:
            rating (float): The desired rating.

        Returns:
            bool: True if the operation was successful, False otherwise.
        """
        return wnp_wrapper.wnp_try_set_rating(self.player, rating)

    def try_toggle_repeat(self):
        """
        Try to toggle the repeat option of the media player.

        Returns:
            bool: True if the operation was successful, False otherwise.
        """
        return wnp_wrapper.wnp_try_toggle_repeat(self.player)