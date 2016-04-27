<!-- This custom XSL tweaks the dblatex XML settings. -->
<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>
  <!-- These parameters disable the list of collaborators and revisions.
       Together remove a useless page from the front matter. -->
  <xsl:param name='doc.collab.show'>0</xsl:param>
  <xsl:param name='latex.output.revhistory'>0</xsl:param>
</xsl:stylesheet>
