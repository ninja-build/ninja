;; Simple emacs mode for editing .ninja files.
;; Just some syntax highlighting for now.

(setq ninja-keywords
      (list
       '("^#.*" . font-lock-comment-face)
       (cons (concat "^" (regexp-opt '("rule" "build" "subninja" "include")
                                     'words))
             font-lock-keyword-face)
       '("\\([[:alnum:]_]+\\) =" . (1 font-lock-variable-name-face))
       ))
(define-derived-mode ninja-mode fundamental-mode "ninja"
  (setq comment-start "#")
  ; Pass extra "t" to turn off syntax-based fontification -- we don't want
  ; quoted strings highlighted.
  (setq font-lock-defaults '(ninja-keywords t))
  )
