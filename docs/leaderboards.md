# LCE Live leaderboards

Minecraft: Wii U Edition did not ship with online leaderboards. This feature
uses the classic Legacy Console leaderboard set found on Xbox and PlayStation:
Traveling, Mining, Farming, and Kills. It uses the game's existing leaderboard
menu and row layout, with Live supplying account-wide results.

The menu keeps the Legacy Console filters:

- **Overall** ranks every Live account with a submitted score.
- **Friends** ranks the signed-in player and their Live friends.
- **My Score** shows the signed-in player's score.

The categories sum the original per-stat columns for their rank, then display
each column separately in the existing game UI. Glide leaderboards are not
included because Wii U Edition did not ship them.

The PC Live bridge passes authenticated requests between the game and API over
the launcher's private temporary directory. The game submits cumulative
profile statistics after its existing leaderboard save/flush points. The API
associates scores with the authenticated account, retains the greatest
submitted value for each column, and pages results by rank. It does not accept
client-supplied usernames or account IDs for score ownership.

Authenticated API routes:

- `GET /v1/leaderboards?board=traveling&filter=overall&start=1&count=15`
- `POST /v1/leaderboards/traveling` with `{"values":[...]}`

Boards are `traveling`, `mining`, `farming`, and `kills`; filters are `overall`,
`friends`, and `mine`. The write endpoint requires a Live bearer session.
