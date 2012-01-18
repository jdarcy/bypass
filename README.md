This is a proof-of-concept translator for an idea that was proposed at FUDcon
2012 in Blacksburg, VA.  The idea is simply that we can forward writes only to
local storage, bypassing AFR but setting the xattrs ourselves to indicate that
self-heal is needed.  This gives us near-local write speeds, and we can mount
later without the bypass to force self-heal when it's convenient.  We can do
almost the same thing for reads as well.  I've tried this and it seems to work,
but there are some major caveats.

* If multiple clients try to write the same file with bypass turned on, you'll
  get *massive* split-brain problems.  Solutions might include honoring AFR's
  quorum-enforcement rules, auto-issuing locks during open to prevent such
  concurrent access, or simply documenting the fact that users must do such
  locking themselves.  The last might sound like a cop-out, but such locking
  is already common for the likely use case of serving virtual-machine images.

* We only intercept readv, writev, and fstat.  There are many other calls that
  can trigger self-heal, including plain old lookup.  The only way to prevent
  a lookup self-heal would be to put another translator *below* AFR to
  intercept xattr requests and pretend everything's OK.  Ick.  Remember,
  though, that this is only a proof of concept.  If we really wanted to get
  serious about this, we could implement the same technique within AFR and do
  all the necessary coordination there.

* It would be nice if the AFR subvolume to use could be specified as an option
  (instead of just picking the first child), if bypass could be made selective,
  etc.

The coolest direction to go here would be to put information about writes we've
seen onto a queue, with a separate process listening on that queue to perform
assynchronous but nearly immediate self-heal on just those files.  As long as
the other consistency issues are handled properly, this might be a really easy
way to get near-local performance for virtual-machine-image use cases without
introducing consistency/recovery nightmares.
