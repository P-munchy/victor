#!/usr/bin/env python

from __future__ import absolute_import
from __future__ import print_function

import inspect
import os
import sys
import textwrap

def _modify_path():
    currentpath = os.path.dirname(inspect.getfile(inspect.currentframe()))
    searchpath = os.path.normpath(os.path.abspath(os.path.join(currentpath, '..', '..')))
    searchpath = os.path.normpath(os.path.abspath(os.path.realpath(searchpath)))
    if searchpath not in sys.path:
        sys.path.insert(0, searchpath)
    sys.path.insert(0, os.path.join('..', '..', 'tools', 'anki-util', 'tools', 'message-buffers'))
_modify_path()

from clad import ast
from clad import clad
from emitters import CPP_emitter

class UnionSwitchEmitter(ast.NodeVisitor):
    "An emitter that generates the handler switch statement."

    groupSwitchPrefix = None
    groupedSwitchMembers = []

    def __init__(self, output=sys.stdout, include_extension=None):
        self.output = output

    def visit_UnionDecl(self, node):
        globals = dict(
            union_name=node.name,
            qualified_union_name=node.fully_qualified_name(),
        )

        self.writeHeader(node, globals)
        self.writeMemberCases(node, globals)
        self.writeFooter(node, globals)

    def writeHeader(self, node, globals):
        self.output.write(textwrap.dedent('''\
        switch(msg.{tagAccessor}) {{
        default:
        \tProcessBadTag_{union_name}(msg.{tagAccessor});
        \tbreak;
        ''').format(tagAccessor = 'tag', **globals))


    def writeFooter(self, node, globals):
        if self.groupedSwitchMembers:
            for member in self.groupedSwitchMembers:
                self.output.write('case {member_tag}:\n'.format(member_tag=member.tag))
            self.output.write('\tProcess_{group_prefix}(msg);\n\tbreak;\n'.format(group_prefix=self.groupSwitchPrefix))
        self.output.write('}\n')

    def writeMemberCases(self, node, globals):
        for member in node.members():
            if self.groupSwitchPrefix is not None and member.name.startswith(self.groupSwitchPrefix):
                self.groupedSwitchMembers.append(member)
            else:
                self.output.write(textwrap.dedent('''\
                    case {member_tag}:
                    \tProcess_{member_name}(msg.{member_name});
                    \tbreak;
                    ''').format(member_tag=member.tag, member_name=member.name, **globals))

if __name__ == '__main__':
    from clad import emitterutil
    from emitters import CPP_emitter

    suffix = '_switch'

    for i, a in enumerate(sys.argv):
        if a.startswith('--group='):
            arg, param = a.split('=')
            UnionSwitchEmitter.groupSwitchPrefix = param
            suffix += '_group_' + param
            del sys.argv[i]
            break

    emitterutil.c_main(language='C++', extension=suffix+'.def',
        emitter_types=[UnionSwitchEmitter],
        use_inclusion_guards=False)
