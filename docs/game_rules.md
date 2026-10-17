# Terrain

- Empty - tanks can drive on it, bullets can fly through it.
- Trees - tanks can drive on it, are displayed under it. Bullets work normally.
- Ice - tanks skid, bullets unaffected.
- Water - tanks cannot drive over, bullets fly over.
- Concrete - one square is made up of 4 cells of concrete. Cannot be destroyed by regular bullets.
- Brick - one square is made up of 16 cells of brick. Can be destroyed by regular bullets.

# Player tank levels

Level 1 - default tank.
Level 2 - tank with faster bullets
Level 3 - tank that can have two bullets in air at the same time
Level 4 - tank that shoots stonger bullets that can destroy concrete.

# Enemy tank levels

Level 1 (basic) - default enemy tank.
Level 2 (fast) - fast enemy tank.
Level 3 (power) - enemy tank with faster bullets.
Level 4 (armor) - armored tank that has armor levels that decrease on hits.

# Powerups

Grenade - destroys all enemies on the screen.
Helmet - adds a protection layer to tank that collected it.
Shovel - adds temporary concrete layer around the eagle.
Star - promotes player up a level.
Tank - additional life.
Timer - stops time for enemies for a moment.

# Gameplay mehanics

- Player shooting another player stuns them.
- Bricks are destroyable by regular bullets.
- Concrete is destroyable by powered bullets (player level 4).
- Powered bullets destroy two layers of brick instead of one.
- Player levels cap at 4, additional stars seem to only give score.
- Players protect a home base eagle.
- Destroying eagle ends game immediately.
- If one player dies the other can continue the game.
- If all players die - game over.
- If all enemies killed - next level.
- Levels are 13x13 squares.
- Single square of brick is made up of 16 smaller cells.
- Single square of concrete is made up of 4 smaller cells.
- Some enemies are "bonus" enemies - when hit they spawn a powerup.
- Hitting powerup armored enemies spawns the powerup but doesn't kill the enemy.
- Powerup appears in a random place on the playfield.
- Construction mode allows player to construct a level.
- Starting the game from construction plays that level, then moves to level 2 normally.
- There is a cheat code to select level.
