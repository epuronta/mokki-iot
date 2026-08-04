# mokki-web

Web UI for the cabin heat pump knob. See the [root README](../README.md) for the system as
a whole and the topic contract.

Replaces the two phones that were each running a hand-configured MQTT client, which shared
nothing and would have meant a third setup for a third phone.

Server-rendered, no SPA and no JSON API. `GET /` renders the current state, `POST /set`
publishes a command and 303-redirects back.

## Stateless by design

Nothing is kept between requests. Each one opens an MQTT connection, does one thing, and
drops it. The broker holds the state in retained messages, so this app can be restarted,
redeployed, or run in two places without losing anything, and nothing has to stay up.

Connections use a random client id, because requests can overlap and two clients sharing
an id would kick each other off mid-request. They also connect clean-session: a durable
session would leave a queue behind, and the broker deletes idle queues after 28 days.

## The two buttons

`150` and `30`, the only positions the knob is ever put in, so there is no slider and no
free-form angle. `POST /set` rejects anything else. The mechanical background is in the
root README; as far as this app is concerned the firmware handles the awkward part and it
just publishes the target.

## Waiting for the echo

After a POST the firmware's echo hasn't arrived yet, so a plain redirect would render the
old position and look like the button did nothing. `POST /set` waits up to 2 seconds for
the echo before redirecting, and falls back to `/?requested=<angle>` so the page can say
"requested, not yet confirmed" instead of lying.

Telling the echo apart from the old value is subtler than it looks. Subscribing to
`mokki/pump-state` replays the retained value immediately, and comparing payloads would
misread that replay as an echo whenever the knob is already at the angle just commanded.
The retain flag is what distinguishes them: a replay arrives with it set, a live publish
with it clear. Verified against RabbitMQ's MQTT plugin, including the same-angle case.

## Other things worth knowing

- Commands are published **retained**, which also fixes the firmware coming up not
  knowing where the knob is, without needing a firmware change.
- Availability comes from `mokki/pump-online`. Without it a retained position from a dead
  controller looks identical to a live one.
- Subscriptions use exact topic names. Wildcard subscriptions never receive retained
  messages, so `mokki/#` would silently come back empty.
- A missing retained value is a normal case, not an impossibility. The broker keeps them
  in an unreplicated node-local store that a restart or migration can drop.
- A broker that can't be reached renders as "not known" rather than a 500. It's the only
  thing the user can see, so it has to explain itself.

## Config

All via environment variables, see `deploy/.env.sample`. TLS on 8883 by default, since
this connects from a VPS across the public internet, unlike the firmware's LAN hop.

## Running locally

```
make install
make run   # http://localhost:8001
```

`make run` picks up `deploy/.env` if it's there, and falls back to whatever is already
exported if it isn't. uv parses the file and passes the values to the child process only,
so nothing is left behind in your shell afterwards.

Don't source that file into your shell by hand instead. It would run as shell code, mangle
the `$` in the bcrypt hash, and leave the wreckage exported, which a later `make deploy`
from the same shell would then prefer over the file itself. Values containing `$` need
single quotes, see `deploy/.env.sample`.

To develop against a throwaway broker instead of the real one, run RabbitMQ with the MQTT
plugin locally:

```
docker run -d --name mokki-rabbit \
  -e RABBITMQ_DEFAULT_USER=mokki -e RABBITMQ_DEFAULT_PASS=mokki \
  -p 11883:1883 -p 15672:15672 rabbitmq:4-management
docker exec mokki-rabbit rabbitmqctl await_startup
docker exec mokki-rabbit rabbitmq-plugins enable rabbitmq_mqtt
```

The plugin is off by default, and the built-in `guest` user is refused from anything that
isn't loopback, which a published port doesn't count as. Then point the app at it with
`MQTT_PORT=11883 MQTT_TLS=0`. With nothing echoing on `mokki/pump-state` every button
press takes the "requested, not yet confirmed" path, which is the one worth looking at
anyway.

## Deploying

Traefik `basicauth` on the router, no unauthenticated route. It controls a heat pump from
the public internet.

```
cp deploy/.env.sample deploy/.env   # fill in, htpasswd -nbB for BASIC_AUTH_USERS
make deploy
```

Expects the shared external `traefik` network to exist already.
