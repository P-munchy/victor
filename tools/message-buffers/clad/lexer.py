"""
The lexer for AnkiBuffers
Author: Mark Pauley
Date: 12/8/2014
"""

from __future__ import absolute_import
from __future__ import print_function

import sys

from . import constants
from .ply import lex
from .ply.lex import TOKEN

class WrappedIntConstant(object):
    
    def __init__(self, text, type, base):
        self.text = text
        self.type = type
        self.value = int(text, base)
    
    def __str__(self):
        return self.text
    
    def __repr__(self):
        return '{0}({1!r}, {2!r}, {3!r})'.format(type(self).__name__, self.text, self.type, self.value)

class WrappedFloatConstant(object):

    def __init__(self, text, type):
        self.text = text
        self.type = type
        self.value = float(text)

    def __str__(self):
        return self.text

    def __repr__(self):
        return '{0}({1!r}, {2!r}, {3!r})'.format(type(self).__name__, self.text, self.type, self.value)


class AnkiBufferLexer(object):
    """ A lexer for the Anki Buffer definition language.  After building it, set the
    input text with input(), and call token() to get new tokens.
    
    The initial filename attribute can be set to an initial filename, but the lexer will update it upon include directives
    """
    def __init__(self,
                 lex_debug= False,
                 type_lookup_func=lambda name: False,                 
                 error_func= lambda message, line, column: False):
        """ Create a new lexer
        error_func:
          An error function. Will be called with an error message,
          line and locumn as arguments, in case of an error during lexing.
        """
        self.error_func = error_func
        self.input_directory = ''
        self.filename = ''
        self.last_token = None
        self.type_lookup_func = type_lookup_func
        self.lex_debug = lex_debug
        
    def build(self, **kwargs):
        """ Builds the lexer from the spec given.  Must be called
        after the lexer object is created.
        The PLY manual warns against calling lex.lex inside __init__
        """
        self.lexer = lex.lex(object=self, **kwargs)
    
    #reset line_no
    def input(self, text):
        self.lexer.input(text)
    
    def token(self):
        self.last_token = self.lexer.token()
        if self.lex_debug:
            print("(Lexer) %s" % self.last_token)
        return self.last_token

    def find_tok_column(self, lexpos):
        """ Find the column of the token in its line
        """
        found = self.lexer.lexdata.rfind('\n', 0, lexpos)
        column = lexpos - max(0, found)
        return column
    
    # Lexer implementation
    def _error(self, msg, t):
        if self.error_func:
            self.error_func(msg, t)
        else:
            sys.exit('{lineno}:{column}: {msg}'.format(lineno=lineno, column=column, msg=msg))
    
    ##
    ## Keywords
    ##
    
    keywords = (
        'bool',
        'float_32',
        'float_64',
        'string',
        'enum',
        'int_8',
        'int_16',
        'int_32',
        'int_64',
        'uint_8',
        'uint_16',
        'uint_32',
        'uint_64',
        'message',
        'structure',
        'union',
        'autounion',
        'namespace',
        'include',
        'no_cpp_class',
        #'tag',
    )
    
    keywords_upper = ()
    keyword_map = {}
    for keyword in keywords:
        keyword_upper = keyword.upper()
        keyword_map[keyword.lower()] = keyword_upper
        keywords_upper = keywords_upper + tuple([keyword_upper])
    
    ##
    ## All the tokens recognized by the lexer
    ##
    tokens = keywords_upper + (
        #Identifiers
        'ID',
        'QUOTED_PATH',
        #Constants
        'INT_CONST_DEC', 'INT_CONST_HEX', 'FLOAT_CONST_DEC',
        #String literals
        #'STRING_LITERAL',
        #Delimiters
        'LBRACE', 'RBRACE',  # { }
        'LSQ', 'RSQ', #[ ]
        'COMMA', # ,
        'EQ', # =
        'COLON', # :
    )
    
    # Regexes
    identifier = r'(?:[a-zA-Z_][0-9a-zA-Z_]*::)*[a-zA-Z_][0-9a-zA-Z_]*'
    
    states = (
    )
    
    # Normal state
    t_ignore = ' \t'

    # Newlines
    def t_NEWLINE(self, t):
        r'\n+'
        t.lexer.lineno += t.value.count("\n")
    
    t_LBRACE = r'\{'
    t_RBRACE = r'\}'
    t_LSQ = r'\['
    t_RSQ = r'\]'
    t_COMMA  = r','
    t_EQ = r'='
    t_COLON = r'\:'
    
    def t_COMMENT(self, t):
        r'(/\*(.|\n)*?\*/)|(//.*)'
        t.lexer.lineno += t.value.count("\n")
                
    @TOKEN(identifier)
    def t_ID(self, t):
        t.type = self.keyword_map.get(t.value, "ID")
        
        if t.type == "ID" and t.value in constants.keywords:
            msg = 'Attempt to use the name "%s", which is a keyword in %s.' % (
                t.value, constants.what_language_uses_keyword(t.value))
            self._error(msg, t)
        
        return t

    def t_QUOTED_PATH(self, t):
        r'\"([0-9a-zA-Z_]+/)*[0-9a-zA-Z_]+(\.[0-9a-zA-Z_]+)?\"'
        # Only allow characters that will work in both C and python.
        # Also, make sure paths are portable.
        return t

    def t_FLOAT_CONST_DEC(self, t):
        r'-?[0-9]+\.[0-9]+'
        t.value = WrappedFloatConstant(t.value, 'dec')
        return t

    def t_INT_CONST_HEX(self, t):
        r'0[xX][0-9a-fA-F]+'
        t.value = WrappedIntConstant(t.value, 'hex', 16)
        return t

    def t_INT_CONST_DEC(self, t):
        r'-?[0-9]+'
        t.value = WrappedIntConstant(t.value, 'dec', 10)
        return t
        
    def t_error(self, t):
        msg = 'Illegal character %s' % repr(t.value[0])
        self._error(msg, t)
        

#######
## TEST MAIN    
def errorFunc(msg, line, column):
    print("Lexical Error: %s(%d,%d)" % (msg, line, column))
    

if __name__ == '__main__':
    import sys
    in_file = "../emitters/tests/src/Foo.clad"
    if len(sys.argv) > 1:
        in_file = sys.argv[1]
    
    test_lexer = AnkiBufferLexer(errorFunc)
    test_lexer.build(optimize=False,debug=1)
    with open(in_file, 'r') as f:
        text = f.read()
        print(text)
        test_lexer.input(text)
        while 1:
            tok = lex.token()
            if not tok: break
            print(tok)

    

