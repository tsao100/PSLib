      PROGRAM PSLIB_TEST
        EXTERNAL START_GUI, REGISTER_ACTION
        EXTERNAL ON_LINE, ON_SAVE, ON_LOAD
        EXTERNAL ON_NEW, ON_SAVE_AS

        CALL REGISTER_ACTION('LINE', ON_LINE)
        CALL REGISTER_ACTION('NEW',  ON_NEW)
        CALL REGISTER_ACTION('OPEN', ON_LOAD)
        CALL REGISTER_ACTION('SAVE', ON_SAVE)
        CALL REGISTER_ACTION('SAVE_AS', ON_SAVE_AS)

        CALL START_GUI
      END

      SUBROUTINE ON_LINE
        EXTERNAL PS_GETPOINT, PS_DRAW_LINE
        DOUBLE PRECISION X1, Y1, X2, Y2
  
        PRINT *, 'Click first point...'
        CALL PS_GETPOINT('Click first point...', X1, Y1, 0)
        X2 = X1
        Y2 = Y1
        PRINT *, 'Click second point...'
        CALL PS_GETPOINT('Click second point...', X2, Y2, 1)
  
        CALL PS_DRAW_LINE(X1, Y1, X2, Y2)
  
        PRINT *, 'Line drawn.'

      END

      SUBROUTINE ON_NEW
        CALL PS_NEW_DRAWING
        PRINT *, 'New Untitled drawing created'
      END

      SUBROUTINE ON_SAVE
        CALL PS_SAVE_CURRENT
      END

      SUBROUTINE ON_SAVE_AS
        EXTERNAL POPUP_FILE_DIALOG
        CALL POPUP_FILE_DIALOG(1)
      END

      SUBROUTINE ON_LOAD
        EXTERNAL POPUP_FILE_DIALOG
        CALL POPUP_FILE_DIALOG(0)
      END

