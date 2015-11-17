<!-- This custom XSL tweaks the DocBook XML -> HTML settings to produce
     an OK-looking manual.  -->
<!DOCTYPE xsl:stylesheet [
<!ENTITY css SYSTEM "style.css">
]>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version='1.0'>
  <xsl:import href="http://docbook.sourceforge.net/release/xsl/current/html/docbook.xsl"/>

  <!-- Embed our stylesheet as the user-provided <head> content. -->
  <xsl:template name="user.head.content"><style>&css;</style></xsl:template>

  <!-- Remove the body.attributes block, which specifies a bunch of
       useless bgcolor etc. attrs on the <body> tag. -->
  <xsl:template name="body.attributes"></xsl:template>

  <!-- Specify that in "book" form (which we're using), we only want a
       single table of contents at the beginning of the document. -->
  <xsl:param name="generate.toc">book toc</xsl:param>

  <!-- Don't put the "Chapter 1." prefix on the "chapters". -->
  <xsl:param name="chapter.autolabel">0</xsl:param>

  <!-- Use <ul> for the table of contents.  By default DocBook uses a
       <dl>, which makes no semantic sense.  I imagine they just did
       it because it looks nice? -->
  <xsl:param name="toc.list.type">ul</xsl:param>

  <xsl:output method="html" encoding="utf-8" indent="no"
              doctype-public=""/>
</xsl:stylesheet>
