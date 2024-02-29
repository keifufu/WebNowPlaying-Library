from wnp import functions as wnp
from time import sleep

wnp.start(1234, "1.0.0") # Start the WNP server with the port 1234 and the Adapter version 1.0.0

for _ in range(60):
    player = wnp.get_active_player(True) # Get the active player and assign the object to player
    print(player.title) # Print the title of the active player
    sleep(1)
    
wnp.stop() # Stop the WNP server