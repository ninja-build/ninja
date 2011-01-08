;; Simple emacs mode for editing .ninja files.
;; Just some syntax highlighting for now.

(setq ninja-keywords
      '(("^#.*" . font-lock-comment-face)
        ("^\\<rule\\>\\|^\\<build\\>\\|^\\<subninja\\>" . font-lock-keyword-face)
        ("\\([[:alnum:]_]+\\) =" . (1 font-lock-variable-name-face))
        ))
(define-derived-mode ninja-mode fundamental-mode "ninja"
  (setq comment-start "#")
  ; Pass extra "t" to turn off syntax-based fontification -- we don't want
  ; quoted strings highlighted.
  (setq font-lock-defaults '(ninja-keywords t))
  )
