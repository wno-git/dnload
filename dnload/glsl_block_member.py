from dnload.common import is_listing
from dnload.glsl_block import GlslBlock
from dnload.glsl_block import extract_tokens

########################################
# GlslBlockMember ######################
########################################

class GlslBlockMember(GlslBlock):
    """Member block."""

    def __init__(self, typeid, name, size=None):
        """Constructor."""
        GlslBlock.__init__(self)
        self.__typeid = typeid
        self.__name = name
        self.__size = size
        # Hierarchy.
        name.setType(typeid)
        self.addNamesUsed(name)

    def format(self, force):
        """Return formatted output."""
        if self.__size:
            return "%s %s[%s];" % (self.__typeid.format(force),
                                   self.__name.format(force),
                                   self.__size.format(force))
        return "%s %s;" % (self.__typeid.format(force), self.__name.format(force))

    def getName(self):
        """Accessor."""
        return self.__name

    def __eq__(self, other):
        """Equals operator."""
        return (self.format(False) == other.format(False))

    def __ne__(self, other):
        """Not equals operator."""
        return not (self == other)

    def __lt__(self, other):
        """Less than operator."""
        return (self.format(False) < other.format(False))

    def __str__(self):
        """String representation."""
        return "Member('%s')" % (self.__name)

########################################
# Functions ############################
########################################

def glsl_parse_member(source):
    """Parse member block."""
    (typeid, name, size, remaining) = extract_tokens(source, ("?t", "?n", "[", "?i", "]", ";"))
    if not typeid:  # struct?
        (typeid, name, size, remaining) = extract_tokens(source, ("?n", "?n", "[", "?i", "]", ";"))
    if typeid:
        return (GlslBlockMember(typeid, name, size), remaining)
    (typeid, name, remaining) = extract_tokens(source, ("?t", "?n", ";"))
    if not typeid:
        (typeid, name, remaining) = extract_tokens(source, ("?n", "?n", ";"))
    if not typeid:
        return (None, source)
    return (GlslBlockMember(typeid, name), remaining)

def glsl_parse_member_list(source):
    """Parse list of members."""
    # Empty member list is ok.
    if is_listing(source) and (0 >= len(source)):
        return []
    (member, content) = glsl_parse_member(source)
    if not member:
        raise RuntimeError("error parsing members: %s" % (str(map(str, source))))
    ret = [member]
    while content:
        (member, remaining) = glsl_parse_member(content)
        if not member:
            raise RuntimeError("error parsing members: %s" % (str(map(str, content))))
        ret += [member]
        content = remaining
    return ret
