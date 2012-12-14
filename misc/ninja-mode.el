;; Copyright 2011 Google Inc. All Rights Reserved.
;;
;; Licensed under the Apache License, Version 2.0 (the "License");
;; you may not use this file except in compliance with the License.
;; You may obtain a copy of the License at
;;
;;     http://www.apache.org/licenses/LICENSE-2.0
;;
;; Unless required by applicable law or agreed to in writing, software
;; distributed under the License is distributed on an "AS IS" BASIS,
;; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
;; See the License for the specific language governing permissions and
;; limitations under the License.

;; Simple emacs mode for editing .ninja files.
;; Just some syntax highlighting for now.

(setq ninja-keywords
      (list
       '("^#.*" . font-lock-comment-face)
       (cons (concat "^" (regexp-opt '("rule" "build" "subninja" "include"
                                       "pool" "default")
                                     'words))
             font-lock-keyword-face)
       '("\\([[:alnum:]_]+\\) =" . (1 font-lock-variable-name-face))
       ;; Variable expansion.
       '("\\($[[:alnum:]_]+\\)" . (1 font-lock-variable-name-face))
       ;; Rule names
       '("rule \\([[:alnum:]_]+\\)" . (1 font-lock-function-name-face))
       ))
(define-derived-mode ninja-mode fundamental-mode "ninja"
  (setq comment-start "#")
  ; Pass extra "t" to turn off syntax-based fontification -- we don't want
  ; quoted strings highlighted.
  (setq font-lock-defaults '(ninja-keywords t))
  )

(provide 'ninja-mode)

;; Run ninja-mode for files ending in .ninja.
;;;###autoload
(add-to-list 'auto-mode-alist '("\\.ninja$" . ninja-mode))
