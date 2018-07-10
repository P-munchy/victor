package token

import (
	"anki/log"
	"anki/token/jwt"
	"anki/util"
	"clad/cloud"
	"time"
)

func initRefresher(done <-chan struct{}) {
	go refreshRoutine(done)
}

func refreshRoutine(done <-chan struct{}) {
	for {
		const tokSleep = 5 * time.Minute
		const ntpSleep = 20 * time.Second

		// wait until we have a valid token
		var tok jwt.Token
		// TODO: this is the code we WILL use once pairing flow exists
		// for {
		// 	tok = jwt.GetToken()
		// 	if tok != nil {
		// 		break
		// 	}
		// 	if util.SleepSelect(tokSleep, done) {
		// 		return
		// 	}
		// }

		// TODO: delete this but for now, null token = fetch one ourselves with a fake request
		for {
			if tok = jwt.GetToken(); tok != nil {
				break
			}
			ch := make(chan *response)
			queue <- request{
				m:  cloud.NewTokenRequestWithAuth(&cloud.AuthRequest{SessionToken: "blahblah"}),
				ch: ch}
			msg := <-ch
			close(ch)
			if tok = jwt.GetToken(); tok != nil {
				log.Println("token refresh: obtained initial fake token")
				break
			}
			log.Println("token refresh: failed fake init, errors:", msg.err, msg.resp.GetAuth().Error)
			log.Println("waiting", tokSleep, "to try again")
			if util.SleepSelect(tokSleep, done) {
				return
			}
		}

		// if robot thinks the token was issued in the future, we have the wrong time and
		// should wait for NTP to figure things out
		for time.Now().Before(tok.IssuedAt()) {
			if util.SleepSelect(ntpSleep, done) {
				return
			}
		}

		// now, time makes sense and we have a token - set a timer for when it should be refreshed
		// add 10s buffer so we're not TOO fast
		refreshDuration := tok.RefreshTime().Sub(time.Now()) + 10*time.Second
		if refreshDuration <= 0 {
			log.Println("token refresh: refreshing")
			ch := make(chan *response)
			defer close(ch)
			queue <- request{m: cloud.NewTokenRequestWithJwt(&cloud.JwtRequest{}), ch: ch}
			msg := <-ch
			if msg.err != nil {
				log.Println("Refresh routine error:", msg.err)
			}
		} else {
			log.Println("token refresh: waiting for", refreshDuration)
			if util.SleepSelect(refreshDuration, done) {
				return
			}
		}
		// either...
		// - we just refreshed a new token
		// - we just waited long enough to get into the current token's refresh period
		// either way, we can just loop back around now and this routine will do the right thing (wait
		// a bunch more or start a refresh right away), while accounting for anything that changed in
		// the meantime (i.e. maybe external forces already forced a token refresh)
	}
}
