package jwt

import (
	"anki/log"
	"anki/robot"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"time"

	"github.com/anki/sai-token-service/model"
	jwt "github.com/dgrijalva/jwt-go"
)

const jwtFile = "token.jwt"

// Token provides the methods that clients will care about for authenticating and
// using tokens
type Token interface {
	IssuedAt() time.Time
	RefreshTime() time.Time
	String() string
	UserID() string
}

// IdentityProvider caches the JWT token for a single robot instance
type IdentityProvider struct {
	path         string
	currentToken *model.Token
}

// NewIdentityProvider creates a new identity provider with path for the JWT file
func NewIdentityProvider(path string) *IdentityProvider {
	if path == "" {
		path = DefaultTokenPath
	}
	return &IdentityProvider{path: path}
}

// ParseToken parses the given token received from the server and saves it
// to our persistent store
func (c *IdentityProvider) ParseToken(token string) (Token, error) {
	tok, err := c.parseToken(token)
	if err != nil {
		return nil, err
	}
	// everything ok, token is legit
	if err := c.saveToken(token); err != nil {
		return nil, err
	}
	c.currentToken = tok
	logUserID(tok)
	return tokWrapper{tok}, nil
}

// Init triggers the jwt package to initialize its data from disk
func (c *IdentityProvider) Init() error {
	err := c.init()
	if err != nil {
		if err := robot.WriteFaceErrorCode(851); err != nil {
			log.Println("Couldn't print face error:", err)
		}
	}
	return err
}

// GetToken returns the current loaded token, if there is one. If this returns
// nil, then one should be requested from the server. If not, it might be worth
// checking ShouldRefresh() on the token to see if a new one should be requested
// anyway.
func (c *IdentityProvider) GetToken() Token {
	if c.currentToken == nil {
		return nil
	}
	return tokWrapper{c.currentToken}
}

func (c *IdentityProvider) init() error {
	// try to create dir token will live in
	if err := os.Mkdir(c.path, 0777); err != nil {
		// if this failed, make sure it's because it already exists
		s, err := os.Stat(c.path)
		if err != nil {
			log.Println("token mkdir + stat error:", err)
			return err
		} else if !s.IsDir() {
			err := fmt.Errorf("token store exists but is not a dir: %s", c.path)
			log.Println(err)
			return err
		}
	}
	// see if a token already lives on disk
	buf, err := ioutil.ReadFile(c.tokenFile())
	if err == nil {
		tok, err := c.parseToken(string(buf))
		if err != nil {
			return err
		}

		// TODO DELETE AFTER SEPTEMBER 7TH-ISH
		// delete fake, no-userid token TMS used to generate for testing
		if tok.UserId == "" {
			log.Println("Deleting old test token")
			os.Remove(c.tokenFile())
			return nil
		}

		c.currentToken = tok
		logUserID(tok)
	}
	return nil
}

func (c *IdentityProvider) tokenFile() string {
	return path.Join(c.path, jwtFile)
}

func (c *IdentityProvider) parseToken(token string) (*model.Token, error) {
	t, _, err := new(jwt.Parser).ParseUnverified(token, jwt.MapClaims{})
	if err != nil {
		return nil, err
	}
	tok, err := model.FromJwtToken(t)
	if err != nil {
		return nil, err
	}
	return tok, nil
}

func (c *IdentityProvider) saveToken(token string) error {
	if err := os.Mkdir(c.path, os.ModeDir); err != nil && !os.IsExist(err) {
		return err
	}
	return ioutil.WriteFile(c.tokenFile(), []byte(token), 0777)
}

func logUserID(token *model.Token) {
	if token == nil {
		return
	}
	if user := token.UserId; user != "" {
		log.Das("profile_id.start", (&log.DasFields{}).SetStrings(user))
	}
}

type tokWrapper struct {
	tok *model.Token
}

func (t tokWrapper) RefreshTime() time.Time {
	return t.tok.ExpiresAt.Add(-3 * time.Hour)
}

func (t tokWrapper) String() string {
	return t.tok.Raw
}

func (t tokWrapper) IssuedAt() time.Time {
	return t.tok.IssuedAt
}

func (t tokWrapper) UserID() string {
	return t.tok.UserId
}
