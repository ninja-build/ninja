;; Simple emacs mode for editing .ninja files.
;; Just some syntax highlighting for now.

(setq ninja-keywords
      '(("^\\<rule\\>\\|^\\<build\\>\\|^\\<subninja\\>" . font-lock-keyword-face)
        ("\\([[:alnum:]_]+\\) =" . (1 font-lock-variable-name-face))
        ))
(define-derived-mode ninja-mode fundamental-mode "ninja"
  (setq font-lock-defaults '(ninja-keywords))
  )
